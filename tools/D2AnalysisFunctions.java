// Adds missing function boundaries in the analysis database, not the executable.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class D2AnalysisFunctions extends GhidraScript {
    @Override public void run() throws Exception {
        if (!"b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180"
                .equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Unsupported executable");
        }
        for (String argument : getScriptArgs()) {
            if (!argument.matches("(?:0x)?[0-9a-fA-F]{1,16}")) throw new IllegalArgumentException(argument);
            Address address = toAddr(argument);
            MemoryBlock block = currentProgram.getMemory().getBlock(address);
            if (block == null || !block.isExecute()) throw new IllegalArgumentException("Not executable: " + address);
            Function function = getFunctionAt(address);
            if (function == null) {
                if (getFunctionContaining(address) != null) throw new IllegalStateException("Inside another function: " + address);
                if (!disassemble(address)) throw new IllegalStateException("Cannot disassemble " + address);
                function = createFunction(address, "d2_candidate_" + address);
            }
            if (function != null) CreateFunctionCmd.fixupFunctionBody(currentProgram, function, monitor);
            if (function == null || !function.getBody().contains(address) || getInstructionAt(address) == null) {
                throw new IllegalStateException("Cannot establish function body " + address);
            }
            println("Analysis function " + address + " body=" + function.getBody());
        }
    }
}
