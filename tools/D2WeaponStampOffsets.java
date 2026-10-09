// Bounded exact-build offline instruction-field inventory; no native game calls.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;
public class D2WeaponStampOffsets extends GhidraScript {
    public void run() throws Exception {
        String sha="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
        if(!sha.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        StringBuilder out=new StringBuilder("sha256="+sha+"\n");int matches=0;
        for(Instruction instruction:currentProgram.getListing().getInstructions(true)) {
            monitor.checkCancelled();String text=instruction.toString();
            if(!text.contains("0x2e7428")) continue;
            if(++matches>256) throw new IllegalStateException("Bound exceeded");
            Function owner=getFunctionContaining(instruction.getAddress());
            out.append(instruction.getAddress()).append(" owner=").append(owner==null?"none":owner.getEntryPoint())
                .append(" ").append(text).append("\n");
        }
        Files.writeString(Path.of(getScriptArgs()[0]),out.toString());
    }
}
