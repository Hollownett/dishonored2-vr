// Read-only: raw rip-relative displacement scan of .text for targets (args: outfile, target...).
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class D2HudRawScan extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        long[] t=new long[a.length-1]; for(int i=1;i<a.length;i++) t[i-1]=Long.parseLong(a[i],16);
        for(MemoryBlock b:currentProgram.getMemory().getBlocks()){
            if(!b.isExecute()||!b.isInitialized()) continue;
            byte[] m=new byte[(int)b.getSize()]; b.getBytes(b.getStart(),m);
            long base=b.getStart().getOffset();
            for(int p=1;p+4<=m.length;p++){
                int disp=(m[p]&255)|((m[p+1]&255)<<8)|((m[p+2]&255)<<16)|((m[p+3]&255)<<24);
                for(int imm:new int[]{0,1,4}){
                    long tg=base+p+4+imm+disp;
                    for(long x:t) if(tg==x){
                        int modrm=m[p-1]&255;
                        if((modrm&0xC7)!=0x05) continue;
                        Address at=b.getStart().add(p-1);
                        Instruction in=getInstructionContaining(at);
                        StringBuilder h=new StringBuilder(); for(int k=Math.max(0,p-4);k<Math.min(m.length,p+8);k++) h.append(String.format("%02x ",m[k]&255));
                        Function f=getFunctionContaining(at);
                        o.append(Long.toHexString(base+p)).append(" tgt=").append(Long.toHexString(x)).append(" imm=").append(imm).append(" fn=").append(f==null?"?":f.getEntryPoint().toString()).append(" insn=").append(in==null?"NONE":in.toString()).append(" bytes=").append(h).append("\n");
                    }
                }
            }
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
