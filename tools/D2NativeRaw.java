// Exact-build bounded instruction export independent of recovered function bodies.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import java.nio.file.Files;
import java.nio.file.Path;
public class D2NativeRaw extends GhidraScript {
    public void run() throws Exception {
        String sha="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
        if(!sha.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        String[] args=getScriptArgs();
        if(args.length<3 || args.length%2!=1) throw new IllegalArgumentException("output, start/end pairs");
        StringBuilder out=new StringBuilder("sha256="+sha+"\n");
        for(int i=1;i<args.length;i+=2) {
            Address at=toAddr(args[i]),end=toAddr(args[i+1]);
            if(end.subtract(at)<1 || end.subtract(at)>8192) throw new IllegalArgumentException("Invalid bounded range");
            while(at.compareTo(end)<0) {
                if(!currentProgram.getMemory().getBlock(at).isExecute()) throw new IllegalStateException("Non-code");
                Instruction instruction=getInstructionAt(at);
                if(instruction==null) {disassemble(at);instruction=getInstructionAt(at);}
                if(instruction==null) throw new IllegalStateException("Cannot decode "+at);
                out.append(at).append(" ").append(instruction).append("\n");
                at=at.add(instruction.getLength());
            }
        }
        Files.writeString(Path.of(args[0]),out.toString());
    }
}
