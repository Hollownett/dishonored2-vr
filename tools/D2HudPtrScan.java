// Read-only: scan non-exec initialized memory for qword values within [lo,hi) (args: outfile, lo, hi).
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.*;
import java.nio.file.*;
import java.nio.*;
public class D2HudPtrScan extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        long lo=Long.parseLong(a[1],16),hi=Long.parseLong(a[2],16);
        for(MemoryBlock b:currentProgram.getMemory().getBlocks()){
            if(!b.isInitialized()) continue;
            byte[] m=new byte[(int)b.getSize()]; b.getBytes(b.getStart(),m);
            ByteBuffer bb=ByteBuffer.wrap(m).order(ByteOrder.LITTLE_ENDIAN);
            for(int p=0;p+8<=m.length;p+=1){ long v=bb.getLong(p); if(v>=lo&&v<hi) o.append(b.getName()).append(" ").append(Long.toHexString(b.getStart().getOffset()+p)).append(" -> ").append(Long.toHexString(v)).append("\n"); }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
