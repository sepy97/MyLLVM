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
        errs() << "Trace BB key: " << TraceKey << "\n";
      TraceMap[TraceKey] = &MBB;
      Checkpointed = true;

    } else if (BBName.find(AbortSubstr) != std::string::npos) {
      unsigned TraceKey = getTraceKey(&BBName);
        errs() << "Abort BB key: " << TraceKey << "\n";
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
      auto AddReg = MRI->createVirtualRegister(&X86::GR64RegClass);

      errs() << "Saving a shifted rsp as checkpoint stack pointer:\n";
      // insert LEA instruction
      // leaq    -10000(%rsp), %ChkptReg
      auto ChkptLEA = BuildMI(TraceMBB, FirstInstruction, FirstDL, XII->get(X86::LEA64r), AddReg);
      ChkptLEA.addReg(X86::RSP);
      ChkptLEA.addImm(1);
      ChkptLEA.addReg(0);
      ChkptLEA.addImm(-10000);
      ChkptLEA.addReg(0);

      errs() << "LEA64r: \n";
      ChkptLEA->print(errs());

      for (auto &MI : TraceMBB) {
        if (MI.mayStore()) {
          // pass MI which are TSX instructions
          if (MI.getOpcode() == X86::XBEGIN || MI.getOpcode() == X86::XEND || MI.getOpcode() == X86::XABORT || MI.getOpcode() == X86::XTEST) {
            errs() << "Skipping TSX instruction:\n";
            MI.print(errs());
            continue;
          }
          MemoryWriteCtr++;

          DebugLoc DL = MI.getDebugLoc();

          const MCInstrDesc &Desc = MI.getDesc();
          int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
          MemRefBegin += X86II::getOperandBias(Desc);

        // move memory address to the virtual register
          auto DestReg = MRI->createVirtualRegister(&X86::GR64RegClass);
          auto MoveDestToReg = BuildMI(TraceMBB, MI, DL, XII->get(X86::LEA64r));
          MoveDestToReg.addDef(DestReg);
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrBaseReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrScaleAmt));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrIndexReg));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrDisp));
          MoveDestToReg.add(MI.getOperand(MemRefBegin + X86::AddrSegmentReg));

        // move memory item to the virtual register
          auto ItemReg = MRI->createVirtualRegister(&X86::GR64RegClass);
            auto MoveItemToReg = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64rm));
            MoveItemToReg.addDef(ItemReg);
            MoveItemToReg.add(MI.getOperand(MemRefBegin + X86::AddrBaseReg));
            MoveItemToReg.add(MI.getOperand(MemRefBegin + X86::AddrScaleAmt));
            MoveItemToReg.add(MI.getOperand(MemRefBegin + X86::AddrIndexReg));
            MoveItemToReg.add(MI.getOperand(MemRefBegin + X86::AddrDisp));
            MoveItemToReg.add(MI.getOperand(MemRefBegin + X86::AddrSegmentReg));

            // push memory address
            auto PushMemAddr = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64mr));
            PushMemAddr.addReg(AddReg);
            PushMemAddr.addImm(1).addReg(0);
            PushMemAddr.addImm(0-16*(MemoryWriteCtr-1)).addReg(0);
            PushMemAddr.addReg(DestReg);

            // move memory item
            auto PushMemItem = BuildMI(TraceMBB, MI, DL, XII->get(X86::MOV64mr));
            PushMemItem.addReg(AddReg);
            PushMemItem.addImm(1).addReg(0);
            PushMemItem.addImm(-8-16*(MemoryWriteCtr-1)).addReg(0);
            PushMemItem.addReg(ItemReg);

        }
      }
      // @@@@@@@@@@@@@@@@ END OF CHECKPOINTING @@@

      // @@@@@@@@@@@@@@@@@@@@@@ ROLLBACK @@@
      errs() << "Number of trace BBs: " << TraceMap.size() << "\nNumber of abort BBs: " << AbortMap.size() << "\n";
      MachineBasicBlock& RollbackMBB = (*AbortMap[Trace.first]);
      MRI = &MF.getRegInfo();
      FirstInstruction = RollbackMBB.instr_begin();
      FirstDL = RollbackMBB.findDebugLoc(FirstInstruction);

      while (MemoryWriteCtr > 0) {
      // Load memory address
        auto MemAddr = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto LoadMemAddr = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rm), MemAddr);
        LoadMemAddr.addReg(AddReg);
        LoadMemAddr.addImm(1).addReg(0);
        LoadMemAddr.addImm(0-16*(MemoryWriteCtr-1)).addReg(0);

        // Load memory item
        auto MemItem = MRI->createVirtualRegister(&X86::GR64RegClass);
        auto LoadMemItem = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64rm), MemItem);
        LoadMemItem.addReg(AddReg);
        LoadMemItem.addImm(1).addReg(0);
        LoadMemItem.addImm(-8-16*(MemoryWriteCtr-1)).addReg(0);

        // move memory item to the memory address
        auto MoveMemItemToAddr = BuildMI(RollbackMBB, FirstInstruction, FirstDL, XII->get(X86::MOV64mr));
        MoveMemItemToAddr.addReg(MemAddr);
        MoveMemItemToAddr.addImm(1).addReg(0);
        MoveMemItemToAddr.addImm(0).addReg(0);
        MoveMemItemToAddr.addReg(MemItem);

        MemoryWriteCtr--;
      }


      // find and remove an instruction that call xabort
      for (auto &I : RollbackMBB) {
        if (I.getOpcode() == X86::XABORT) {
          I.eraseFromParent();
          break;
        }
      }
      // @@@@@@@@@@@@@@@@ END OF ROLLBACK @@@
    errs() << "Modified Trace MBB:\n";
    TraceMBB.dump();
    errs() << "Modified Rollback MBB:\n";
    RollbackMBB.dump();
  }

  // find and remove an instruction that call xend from CommitBasicBlock
  for (auto &I : *CommitBasicBlock) {
    if (I.getOpcode() == X86::XEND) {
      I.eraseFromParent();
      break;
    }
  }

  errs() << "FINISHED Checkpointing pass!\n";
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
    unsigned numOfTokens = tokens.size();
    return ((unsigned) std::stoi(tokens[numOfTokens-1]));
}
