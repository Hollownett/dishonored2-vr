// Read-only disassembly, including blocks omitted by recovered function bodies.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import java.nio.file.Files;
import java.nio.file.Path;
public class D2AimBlocks extends GhidraScript {
    public void run() throws Exception {
        String sha = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
        if (!sha.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        StringBuilder out = new StringBuilder("sha256=" + sha + "\n");
        Address at=toAddr("1407f6526"), end=toAddr("1407f68fb");
        while (at.compareTo(end)<0) {
            Instruction instruction=getInstructionAt(at);
            if (instruction==null) {
                // Disassembly changes only the ephemeral read-only analysis transaction.
                disassemble(at); instruction=getInstructionAt(at);
            }
            if (instruction==null) throw new IllegalStateException("Cannot decode " + at);
            out.append(at).append(" ").append(instruction).append("\n");
            at=at.add(instruction.getLength());
        }
        for (String table : new String[]{"1407f69f0","1407f6a34"}) {
            out.append("table=").append(table).append("\n");
            for (int i=0;i<17;i++) out.append(i).append(" -> ")
                .append(Long.toHexString(0x140000000L + Integer.toUnsignedLong(getInt(toAddr(table).add(i*4)))))
                .append("\n");
        }
        Files.writeString(Path.of(getScriptArgs()[0]),out.toString());
    }
}
