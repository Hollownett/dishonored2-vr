// Read-only: dump vtable slots (args: out, vtableAddr...) with function size and string refs count.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2TakedownVtable extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        for(int i=1;i<a.length;i++){
            Address v=toAddr(a[i]); o.append("vtable ").append(v).append("\n");
            for(int s=0;s<40;s++){
                long p=currentProgram.getMemory().getLong(v.add(s*8L));
                if(p<0x140000000L||p>0x144000000L) break;
                Address fa=toAddr(p); Function f=getFunctionContaining(fa);
                o.append(String.format("  [%d] %s size=%d%n",s,fa,f==null?0:f.getBody().getNumAddresses()));
            }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
