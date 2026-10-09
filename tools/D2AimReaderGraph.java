// Bounded read-only direct caller graph for camera-aim field readers.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.HashSet;
public class D2AimReaderGraph extends GhidraScript {
    public void run() throws Exception {
        String sha="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
        if(!sha.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        StringBuilder out=new StringBuilder("sha256="+sha+"\nkind\tfrom\tto\n");
        ArrayDeque<Function> pending=new ArrayDeque<>();HashSet<Address> seen=new HashSet<>();
        String[] args=getScriptArgs();
        for(int i=1;i<args.length;i++) {
            Function f=getFunctionContaining(toAddr(args[i]));
            if(f==null) throw new IllegalStateException("No function for "+args[i]);
            out.append("root\t").append(args[i]).append("\t").append(f.getEntryPoint()).append("\n");
            pending.add(f);
        }
        while(!pending.isEmpty()) {
            monitor.checkCancelled();Function f=pending.remove();
            if(!seen.add(f.getEntryPoint()))continue;
            if(seen.size()>1500)throw new IllegalStateException("Graph bound exceeded");
            for(Function caller:f.getCallingFunctions(monitor)) {
                out.append("caller\t").append(f.getEntryPoint()).append("\t").append(caller.getEntryPoint()).append("\n");
                pending.add(caller);
            }
            out.append("node\t").append(f.getEntryPoint()).append("\t").append(f.getName()).append("\n");
        }
        Files.writeString(Path.of(args[0]),out.toString());
    }
}
