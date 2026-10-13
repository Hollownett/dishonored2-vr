// Read-only: instruction text grep over a range (args: outfile, start, end, substr...).
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2HudGrep extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        Address s=toAddr(a[1]),e=toAddr(a[2]);
        InstructionIterator it=currentProgram.getListing().getInstructions(s,true);
        while(it.hasNext()){
            Instruction in=it.next();
            if(in.getAddress().compareTo(e)>0) break;
            String t=in.toString();
            for(int i=3;i<a.length;i++) if(t.contains(a[i])){
                Function f=getFunctionContaining(in.getAddress());
                o.append(in.getAddress()).append(" ").append(f==null?"?":f.getEntryPoint().toString()).append("  ").append(t).append("\n");break;}
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
