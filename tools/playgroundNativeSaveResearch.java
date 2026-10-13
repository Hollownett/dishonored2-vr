// Read-only pinned-binary research for native save isolation, never a game launch.
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.util.LinkedHashSet;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.listing.FlowOverride;

public class playgroundNativeSaveResearch extends GhidraScript {
    public void run() throws Exception {
        if(!"b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180".equalsIgnoreCase(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Unsupported executable");
        LinkedHashSet<Function> functions=new LinkedHashSet<>();
        Function logger=getFunctionAt(toAddr(0x140166830L));
        if(logger!=null)logger.setNoReturn(false); // analysis-only; readOnly discards this repair
        for(long offset:new long[]{0x14003f8f0L,0x14003fbf0L,0x140160610L,0x140041240L,0x140155d80L,
            0x1400dcb10L,0x1400e70f9L,0x1400e71f4L}) {
            Address address=toAddr(offset);Function f=getFunctionAt(address);
            if(f==null) {disassemble(address);f=createFunction(address,null);}
            if(f!=null) {
                for(int pass=0;pass<8;++pass) {
                    for(Reference ref:currentProgram.getReferenceManager().getReferencesTo(toAddr(0x140166830L))) {
                        long at=ref.getFromAddress().getOffset();
                        if(at<offset || at>=offset+(offset==0x14003fbf0L?0x1100:0x400))continue;
                        var instruction=getInstructionAt(ref.getFromAddress());
                        if(instruction!=null && instruction.getFlowType().isCall()) {
                            instruction.setFlowOverride(FlowOverride.NONE);
                            disassemble(ref.getFromAddress().add(instruction.getLength()));
                        }
                    }
                    CreateFunctionCmd.fixupFunctionBody(currentProgram,f,monitor);
                }
                functions.add(f);
            }
        }
        for(long offset:new long[]{0x1432c71f0L,0x141ac58a8L,0x141ac58b8L,0x141ac58d0L,0x141ac5968L,
            0x141e5b9c0L,0x141adf7c8L}) {
            Address address=toAddr(offset);
            println("SAVE_OBJECT "+address);
            for(Reference ref:currentProgram.getReferenceManager().getReferencesTo(address)) {
                Function f=getFunctionContaining(ref.getFromAddress());
                println("SAVE_REF "+ref.getFromAddress()+" "+f);
                if(f!=null)functions.add(f);
            }
        }
        DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
        int count=0;
        for(Function f:functions) {
            if(++count>25)break;
            println("SAVE_FUNCTION "+f.getEntryPoint()+" "+f.getName());
            var result=decomp.decompileFunction(f,20,monitor);
            if(result.decompileCompleted())println(result.getDecompiledFunction().getC());
            else println("DECOMPILE_FAILED "+result.getErrorMessage());
        }
        decomp.dispose();
    }
}
