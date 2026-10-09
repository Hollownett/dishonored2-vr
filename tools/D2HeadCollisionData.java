// Bounded read-only exact-build data export for native lean callback discovery.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.nio.file.Files;
import java.nio.file.Path;
public class D2HeadCollisionData extends GhidraScript {
    public void run() throws Exception {
        String sha="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
        if(!sha.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        String[] args=getScriptArgs();
        if(args.length!=2) throw new IllegalArgumentException("output, table address");
        Address base=toAddr(args[1]);
        StringBuilder out=new StringBuilder("sha256="+sha+"\n");
        for(int i=0;i<32;++i) {
            Address at=base.add(i*8);long value=currentProgram.getMemory().getLong(at);
            out.append(at).append(" ").append(Long.toHexString(value)).append("\n");
        }
        Files.writeString(Path.of(args[0]),out.toString());
    }
}
