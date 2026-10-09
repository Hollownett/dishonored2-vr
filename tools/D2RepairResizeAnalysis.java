// Repairs analysis metadata only, never the executable.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.symbol.Reference;

public class D2RepairResizeAnalysis extends GhidraScript {
    @Override public void run() throws Exception {
        if (!"b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"
                .equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Unsupported executable");
        }
        Address logger = toAddr(0x140166830L);
        Function helper = getFunctionAt(logger);
        if (helper == null) throw new IllegalStateException("Missing logging helper");
        helper.setNoReturn(false);
        for (long entry : new long[]{0x140368b80L, 0x14036aa40L}) {
            Function function = getFunctionAt(toAddr(entry));
            if (function == null) throw new IllegalStateException("Missing resize function");
            for (Reference ref : currentProgram.getReferenceManager().getReferencesTo(logger)) {
                Address call = ref.getFromAddress();
                if (call.getOffset() < entry || call.getOffset() >= entry + 0x800) continue;
                Instruction instruction = getInstructionAt(call);
                if (instruction == null || !instruction.getFlowType().isCall()) continue;
                instruction.setFlowOverride(FlowOverride.NONE);
                disassemble(call.add(instruction.getLength()));
            }
            if (!CreateFunctionCmd.fixupFunctionBody(currentProgram, function, monitor)) {
                throw new IllegalStateException("Cannot repair resize flow " + function.getEntryPoint());
            }
            println("Repaired analysis flow " + function.getEntryPoint() + " body=" + function.getBody());
        }
    }
}
