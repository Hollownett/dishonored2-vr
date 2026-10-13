// Read-only: decompile every function in [start,end) (args: out, start, end) and keep only those whose C matches regex arg4 (optional).
// @category Dishonored2
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.util.regex.*;
public class D2CrouchRange extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs();
        DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
        StringBuilder o=new StringBuilder();
        Pattern p=a.length>3?Pattern.compile(a[3]):null;
        Address s=toAddr(a[1]), e=toAddr(a[2]);
        for(Function f:currentProgram.getFunctionManager().getFunctions(s,true)){
            if(f.getEntryPoint().compareTo(e)>=0) break;
            DecompileResults r=d.decompileFunction(f,30,monitor);
            if(!r.decompileCompleted()) continue;
            String c=r.getDecompiledFunction().getC();
            if(p!=null&&!p.matcher(c).find()) continue;
            o.append("//// ").append(f.getEntryPoint()).append(" size=").append(f.getBody().getNumAddresses()).append("\ncallers:");
            for(Function x:f.getCallingFunctions(monitor)) o.append(" ").append(x.getEntryPoint());
            o.append("\n").append(c).append("\n");
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
