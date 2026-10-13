// Read-only: list references to addresses (args: outfile, addr[:len]...), with containing function.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
public class D2HudRefs extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        for(int i=1;i<a.length;i++){
            String[] pr=a[i].split(":"); int len=pr.length>1?Integer.parseInt(pr[1]):1;
            Address a0=toAddr(pr[0]); o.append("== ").append(a0).append(" len ").append(len).append("\n");
            for(int k=0;k<len;k++){
                Address ad=a0.add(k);
                for(Reference r:getReferencesTo(ad)){
                    Function f=getFunctionContaining(r.getFromAddress());
                    Instruction in=getInstructionAt(r.getFromAddress());
                    o.append("  +").append(k).append(" ").append(r.getFromAddress()).append(" ").append(r.getReferenceType()).append(" ").append(f==null?"?":f.getEntryPoint().toString()).append("  ").append(in==null?"":in.toString()).append("\n");
                }
            }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
