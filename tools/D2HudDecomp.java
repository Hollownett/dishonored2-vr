// Read-only: decompile functions (args: outfile, addr...), append callers/callees and decompiled C.
// @category Dishonored2
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.util.*;
public class D2HudDecomp extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs();
        DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
        StringBuilder o=new StringBuilder();
        for(int i=1;i<a.length;i++){
            Address ad=toAddr(a[i]);
            Function f=getFunctionContaining(ad);
            if(f==null){ disassemble(ad); f=createFunction(ad,null); }
            if(f==null){o.append("//// no function at ").append(a[i]).append("\n");continue;}
            o.append("//// ").append(f.getEntryPoint()).append(" size=").append(f.getBody().getNumAddresses()).append("\ncallers:");
            for(Function c:f.getCallingFunctions(monitor)) o.append(" ").append(c.getEntryPoint());
            o.append("\ncallees:");
            for(Function c:f.getCalledFunctions(monitor)) o.append(" ").append(c.getEntryPoint());
            o.append("\n");
            DecompileResults r=d.decompileFunction(f,60,monitor);
            o.append(r.decompileCompleted()?r.getDecompiledFunction().getC():"ERR "+r.getErrorMessage()).append("\n");
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
