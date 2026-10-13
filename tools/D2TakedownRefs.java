// Read-only: list all references (incl. data) to addresses (args: out, addr...), with containing function.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
public class D2TakedownRefs extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        for(int i=1;i<a.length;i++){
            Address ad=toAddr(a[i]); o.append("refs to ").append(ad).append("\n");
            for(Reference r:getReferencesTo(ad)){
                Function f=getFunctionContaining(r.getFromAddress());
                o.append("  ").append(r.getFromAddress()).append(" ").append(r.getReferenceType()).append(" in ").append(f==null?"-":f.getEntryPoint().toString()).append("\n");
            }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
