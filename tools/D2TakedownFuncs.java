// Read-only: list functions in [start,end) (args: out, start, end) with caller counts.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2TakedownFuncs extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        Address s=toAddr(a[1]), e=toAddr(a[2]);
        for(Function f:currentProgram.getFunctionManager().getFunctions(s,true)){
            if(f.getEntryPoint().compareTo(e)>=0) break;
            o.append(f.getEntryPoint()).append(" size=").append(f.getBody().getNumAddresses()).append(" callers:");
            for(Function c:f.getCallingFunctions(monitor)) o.append(" ").append(c.getEntryPoint());
            o.append("\n");
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
