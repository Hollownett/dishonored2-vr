// Read-only: dump qword pointers at addr (args: out, addr, count) with function sizes.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2TakedownPtrs extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        Address v=toAddr(a[1]); int n=Integer.parseInt(a[2]);
        for(int s=0;s<n;s++){
            long p=currentProgram.getMemory().getLong(v.add(s*8L));
            Function f=(p>=0x140000000L&&p<0x144000000L)?getFunctionContaining(toAddr(p)):null;
            o.append(String.format("%s +%x: %x %s%n",v.add(s*8L),s*8,p,f==null?"":("fn size="+f.getBody().getNumAddresses())));
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
