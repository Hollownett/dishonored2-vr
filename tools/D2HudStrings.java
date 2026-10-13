// Read-only: scan memory for ASCII strings matching regex (args: out, comma-separated regex) and list xref functions.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.nio.file.*;
import java.util.regex.*;
public class D2HudStrings extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs();
        StringBuilder o=new StringBuilder();
        Pattern p=Pattern.compile(a[1].replace(",","|"),Pattern.CASE_INSENSITIVE);
        for(MemoryBlock b:currentProgram.getMemory().getBlocks()){
            if(!b.isInitialized()||b.isExecute()) continue;
            byte[] buf=new byte[(int)b.getSize()];
            b.getBytes(b.getStart(),buf);
            int i=0;
            while(i<buf.length){
                int j=i;
                while(j<buf.length&&buf[j]>=0x20&&buf[j]<0x7f) j++;
                if(j-i>=4&&j<buf.length&&buf[j]==0){
                    String s=new String(buf,i,j-i);
                    if(p.matcher(s).find()){
                        Address ad=b.getStart().add(i);
                        o.append(ad).append(" \"").append(s).append("\"\n");
                        for(Reference r:getReferencesTo(ad)){
                            Function f=getFunctionContaining(r.getFromAddress());
                            o.append("    xref ").append(r.getFromAddress()).append(" in ").append(f==null?"?":f.getName()+"@"+f.getEntryPoint()).append("\n");
                        }
                    }
                }
                i=j+1;
            }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
