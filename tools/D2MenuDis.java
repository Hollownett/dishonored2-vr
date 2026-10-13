// Read-only: disassemble a range (args: outfile, start, end).
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2MenuDis extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        Address s=toAddr(a[1]),e=toAddr(a[2]);
        Address c=s;
        while(c.compareTo(e)<=0){
            Instruction in=getInstructionAt(c);
            if(in==null){ disassemble(c); in=getInstructionAt(c);} 
            if(in==null){ o.append(c).append(" ??\n"); c=c.add(1); continue; }
            o.append(c).append("  ").append(in.toString()).append("\n"); c=c.add(in.getLength());
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
