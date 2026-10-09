// Read-only exports from a Ghidra project made from the pinned Steam executable.
// @category Dishonored2

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class D2StaticResearch extends GhidraScript {
    private static final String SHA256 =
        "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";

    @Override
    public void run() throws Exception {
        if (!SHA256.equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Executable SHA-256 differs from the pinned game");
        }
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("Output directory and addresses required");
        }
        Path output = Path.of(args[0]).toAbsolutePath().normalize();
        Files.createDirectories(output);
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) {
                throw new IllegalStateException(decompiler.getLastMessage());
            }
            Set<Address> exported = new HashSet<>();
            for (int i = 1; i < args.length; ++i) {
                monitor.checkCancelled();
                boolean referenceQuery = args[i].startsWith("ref:");
                String hex = referenceQuery ? args[i].substring(4) : args[i];
                if (!hex.matches("(?:0x)?[0-9a-fA-F]{1,16}")) {
                    throw new IllegalArgumentException("Invalid address: " + hex);
                }
                Address address = toAddr(hex);
                StringBuilder evidence = new StringBuilder("sha256=" + SHA256 + "\nquery=" + address + "\n");
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
                List<Function> selected = new ArrayList<>();
                Function containing = getFunctionContaining(address);
                if (!referenceQuery && containing != null) selected.add(containing);
                int count = 0;
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Function caller = getFunctionContaining(ref.getFromAddress());
                    evidence.append(ref.getFromAddress()).append(" ").append(ref.getReferenceType())
                        .append(" function=").append(caller == null ? "unknown" : caller.getEntryPoint()).append('\n');
                    // Generated type registration can exceed the decompiler's flow limit.
                    // Keep the bounded instruction window that actually mentions the query.
                    if (caller != null && caller.getBody().getNumAddresses() > 65536 && count < 20) {
                        Address start = ref.getFromAddress().subtract(120);
                        Address end = ref.getFromAddress().add(220);
                        InstructionIterator instructions = currentProgram.getListing().getInstructions(start, true);
                        while (instructions.hasNext()) {
                            Instruction instruction = instructions.next();
                            if (instruction.getAddress().compareTo(end) > 0) break;
                            evidence.append("  ").append(instruction.getAddress()).append(' ')
                                .append(instruction).append('\n');
                        }
                    }
                    if (referenceQuery && caller != null && !selected.contains(caller) && selected.size() < 12) {
                        selected.add(caller);
                    }
                    if (++count >= 500) { evidence.append("reference export truncated at 500\n"); break; }
                }
                Files.writeString(output.resolve("query-" + address + ".txt"), evidence, StandardCharsets.UTF_8);
                selected.sort(Comparator.comparing(Function::getEntryPoint));
                for (Function function : selected) {
                    if (!exported.add(function.getEntryPoint())) continue;
                    if (function.getBody().getNumAddresses() > 65536) {
                        println("Skipped large function " + function.getEntryPoint() + "; see reference instruction windows");
                        continue;
                    }
                    DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
                    StringBuilder text = new StringBuilder("sha256=" + SHA256 + "\nentry=" + function.getEntryPoint()
                        + "\nbody=" + function.getBody() + "\n");
                    text.append("callees:\n");
                    function.getCalledFunctions(monitor).stream().sorted(Comparator.comparing(Function::getEntryPoint))
                        .forEach(f -> text.append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n'));
                    text.append("callers:\n");
                    function.getCallingFunctions(monitor).stream().sorted(Comparator.comparing(Function::getEntryPoint))
                        .limit(100).forEach(f -> text.append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n'));
                    text.append("decompilation_completed=").append(result.decompileCompleted()).append('\n');
                    if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                        text.append(result.getDecompiledFunction().getC());
                    } else {
                        text.append("error=").append(result.getErrorMessage()).append('\n');
                    }
                    text.append("\ninstructions (at most 640):\n");
                    InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                    int instructionCount = 0;
                    while (instructions.hasNext() && instructionCount++ < 640) {
                        Instruction instruction = instructions.next();
                        text.append(instruction.getAddress()).append(' ').append(instruction).append('\n');
                    }
                    if (instructions.hasNext()) text.append("instruction export truncated\n");
                    Files.writeString(output.resolve("function-" + function.getEntryPoint() + ".txt"), text, StandardCharsets.UTF_8);
                    println("Exported " + function.getEntryPoint() + " completed=" + result.decompileCompleted());
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
