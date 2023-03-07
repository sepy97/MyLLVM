//
// Created by Semen Pyankov on 12/17/22.
//

#include "Disasm_husky.h"

bool Disasm_husky::runOnMachineFunction(MachineFunction &MF) {

  const auto &SubTarget = MF.getSubtarget();
  auto XII = SubTarget.getInstrInfo();

  bool Checkpointed = false; // condition shows if we entered the speculative region
  bool Rolledback   = false; // condition shows if we exited the speculative region and rolled back memory writes
  bool Committed    = false; // condition shows if we exited the speculative region normally


  for (auto &MBB: MF) {
    std::string BBName = MBB.getFullName();
    if (BBName.find(TraceSubstr) != std::string::npos) {
      unsigned TraceKey = getTraceKey(&BBName);
      TraceMap[TraceKey] = &MBB;
      Checkpointed = true;

    } else if (BBName.find(AbortSubstr) != std::string::npos) {
      unsigned TraceKey = getTraceKey(&BBName);
      AbortMap[TraceKey] = &MBB;
      Rolledback = true;

    } else if (BBName.find(CommitSubstr) != std::string::npos) {
      CommitBasicBlock = &MBB;
      Committed = true;
    }
  }

  if (!Checkpointed) return false; // no checkpointing needed
  if (!Rolledback && !Committed) return false; // code has to be either rolled back or committed if it was checkpointed

  // iterate through TraceMap keys and insert checkpoints
  for (auto &Trace : TraceMap) {
    MachineBasicBlock& TraceMBB = (*Trace.second);

    unsigned MemoryWriteCtr = 0; // counter of checkpointed memory writes

    // @@@@@@@@@@@@@@@@@@@@@@ CHECKPOINT @@@
      auto MRI = &MF.getRegInfo();
      auto FirstInstruction = TraceMBB.instr_begin();
      DebugLoc FirstDL = TraceMBB.findDebugLoc(FirstInstruction);
      auto ChkptReg = MRI->createVirtualRegister(&X86::GR64RegClass);

      // TODO: instead of those two instructions, add a single one (movq $-10000(%rsp), %rcx)
      // via .addReg(rsp).addImm(-10000)

      errs() << "Saving a shifted rsp as checkpoint stack pointer:\n";
      // movq    $-10000, %ChkptReg
      auto ChkptInit = BuildMI(TraceMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64ri32), ChkptReg);
      ChkptInit.addImm(-10000);
      ChkptInit->print(errs());

      // addq    %rsp, %ChkptReg
      auto AddChkpt = BuildMI(TraceMBB, FirstInstruction, FirstDL, XII->get(X86::ADD64rr), ChkptReg);
      AddChkpt.addReg(ChkptReg);
      AddChkpt.addReg(X86::RSP);
      AddChkpt->print(errs());

      for (auto &MI : TraceMBB) {
        if (MI.mayStore()) {
          MemoryWriteCtr++;

          DebugLoc DL = MI.getDebugLoc();

          const MCInstrDesc &Desc = MI.getDesc();
          int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
          MemRefBegin += X86II::getOperandBias(Desc);

          // Create virtual register and save RSP to the reserved register
          auto VReg = MRI->createVirtualRegister(&X86::GR64RegClass);
          auto SaveRsp = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64rr), VReg);
          SaveRsp.addReg(X86::RSP);

          // change RSP to checkpointSP
          auto ShiftRsp = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64rr), X86::RSP);
          ShiftRsp.addReg(ChkptReg);

          // TODO: next two instructions should be in one instruction (PUSH64m with memory address as an operand)
          // move memory address to the virtual register
          auto DestReg = MRI->createVirtualRegister(&X86::GR64RegClass);
          auto MoveDestToReg = BuildMI(TraceMBB, MI, DL, XII->get(X86::LEA64r));
          MoveDestToReg.addDef(DestReg);
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrBaseReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrScaleAmt));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrIndexReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrDisp));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrSegmentReg));

          // push memory address
          auto PushMemAddr = BuildMI(TraceMBB, MI, DL, XII->get(X86::PUSH64r));
          PushMemAddr.addReg(DestReg);

          // push memory item
          auto PushMemItem = BuildMI(TraceMBB, MI, DL, XII->get(X86::PUSH64rmm)); // TODO: USE MachineIRBuilder @@@
          PushMemItem.addReg(DestReg);
          PushMemItem.addImm(1).addReg(0);
          PushMemItem.addImm(0).addReg(0);

          // save RSP to checkpointSP (checkpointSP should change bcoz we already checkpointed two more values)
          auto ShiftChkpt = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64rr), ChkptReg);
          ShiftChkpt.addReg(X86::RSP);

          // change RSP back (get it from the reserved register)
          auto RestoreRsp = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64rr), X86::RSP);
          RestoreRsp.addReg(VReg);
        }
      }
      // @@@@@@@@@@@@@@@@ END OF CHECKPOINTING @@@

      // @@@@@@@@@@@@@@@@@@@@@@ ROLLBACK @@@
      MachineBasicBlock& RollbackMBB = (*AbortMap[Trace.first]);
      MRI = &MF.getRegInfo();
      FirstInstruction = RollbackMBB.instr_begin();
      FirstDL = RollbackMBB.findDebugLoc(FirstInstruction);

      // Save rsp
      auto SaveRsp = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr));
      auto VReg = MRI->createVirtualRegister(&X86::GR64RegClass);
      SaveRsp.addDef(VReg);
      SaveRsp.addReg(X86::RSP);

      // Change rsp to checkpointSP
      auto ShiftRsp = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr), X86::RSP);
      ShiftRsp.addReg(ChkptReg);

      while (MemoryWriteCtr > 0) {
        MemoryWriteCtr--;

        // pop memory item
        auto MemItem = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto PopMemItem = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::POP64r));
        PopMemItem.addDef(MemItem);

        // pop memory address
        auto MemAddr = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto PopMemAddr = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::POP64r));
        PopMemAddr.addDef(MemAddr);

        // move memory item to the memory address
        auto MoveMemItemToAddr = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64mr));
        MoveMemItemToAddr.addReg(MemAddr);
        MoveMemItemToAddr.addImm(1).addReg(0);
        MoveMemItemToAddr.addImm(0).addReg(0);
        MoveMemItemToAddr.addReg(MemItem);
      }

      // change RSP back (get it from the reserved register)
      auto RestoreRsp = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rr), X86::RSP);
      RestoreRsp.addReg(VReg);

      // find and remove an instruction that call xabort
      for (auto &I : RollbackMBB) {
        if (I.getOpcode() == X86::XABORT) {
          I.eraseFromParent();
          break;
        }
      }

      // @@@@@@@@@@@@@@@@ END OF ROLLBACK @@@

  }

  // find and remove an instruction that call xend from CommitBasicBlock
  for (auto &I : *CommitBasicBlock) {
    if (I.getOpcode() == X86::XEND) {
      I.eraseFromParent();
      break;
    }
  }

  return false;
}

unsigned Disasm_husky::getTraceKey(std::string* BBName) {
  // split the BBName by an underscore delimiter to get the trace key
    std::string LocalBBName = *BBName;
    std::vector<std::string> tokens;
    std::string delimiter = "_";
    std::string token;
    while ((token = LocalBBName.substr(0, LocalBBName.find(delimiter))) != "") {
        tokens.push_back(token);
        LocalBBName.erase(0, token.length() + delimiter.length());
    }

    // the trace key is the second token
    return ((unsigned) std::stoi(tokens[1]));
}
