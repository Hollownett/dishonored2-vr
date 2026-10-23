// Task B6 (hand-body static RE, docs/native-hand-body-re.md): batch query script for the pinned build.
// Run headless on an already analysed project, read-only, no re-analysis:
//   analyzeHeadless <proj> D2 -process Dishonored2.exe -noanalysis -readOnly -scriptPath tools \
//       -postScript D2HandBodyQ.java <out.txt> <commands.txt>
// One command per line, "<op>:<arg>", '#' starts a comment. Addresses are RVAs (image base 0x140000000) or VAs.
//   d:RVA          decompile the function containing RVA
//   i:RVA,N        N instructions from RVA (disassembles if needed)
//   b:RVA,N        N raw bytes at RVA
//   x:RVA          references to RVA (with containing function)
//   c:RVA          callers of the function containing RVA
//   a:RVA,N        for every call reference to RVA: the N instructions before the call and the call itself
//   v:RVA,N        N qwords at RVA (vtable dump), function entries marked
//   s:text         ASCII string search (first 6 hits) plus references to each hit
//   k:LO-HI        every instruction with a scalar operand in [LO,HI] (whole program; slow)
//   n:RVA          nearest symbol at or below RVA (vtable/RTTI naming)
//   f:RVA,N        N floats at RVA
//   g:regex        every instruction whose text matches regex (whole program; first 400)
//   w:text         symbols whose qualified name contains text (first 80; vftables and RTTI names)
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;
public class D2HandBodyQ extends GhidraScript {
    static final String SHA="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
    DecompInterface di; StringBuilder out=new StringBuilder();
    Address A(String s){ long v=Long.decode(s.trim()); if(v<0x140000000L) v+=0x140000000L; return toAddr(v);}
    public void run() throws Exception {
        if(!SHA.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Wrong build");
        String[] a=getScriptArgs();
        if(a.length!=2) throw new IllegalArgumentException("usage: out.txt commands.txt");
        di=new DecompInterface(); di.openProgram(currentProgram);
        out.append("sha256=").append(SHA).append("\n");
        for(String c: Files.readAllLines(Path.of(a[1]))) {
            c=c.trim(); if(c.isEmpty()||c.startsWith("#")) continue;
            int colon=c.indexOf(':'); String k=c.substring(0,colon), v=c.substring(colon+1);
            String[] p=v.split(",");
            out.append("\n=== ").append(c).append("\n");
            try { switch(k) {
                case "d": decomp(A(p[0])); break;
                case "i": { Address at=A(p[0]); int n=p.length>1?Integer.parseInt(p[1].trim()):60;
                    for(int j=0;j<n;j++){ Instruction ins=getInstructionAt(at); if(ins==null){ disassemble(at); ins=getInstructionAt(at);} if(ins==null){out.append("undecodable "+at+"\n");break;}
                        out.append(at).append("  ").append(ins).append("\n"); at=at.add(ins.getLength()); } break; }
                case "b": { int n=p.length>1?Integer.parseInt(p[1].trim()):32; byte[] b=new byte[n]; currentProgram.getMemory().getBytes(A(p[0]),b);
                    StringBuilder s=new StringBuilder(); for(byte x:b) s.append(String.format("%02x ",x)); out.append(s.toString().trim()).append("\n"); break; }
                case "x": xrefs(A(p[0])); break;
                case "c": { Function f=getFunctionContaining(A(p[0])); if(f==null){out.append("nofunc\n");break;}
                    out.append("func "+f.getName()+" @"+f.getEntryPoint()+"\n");
                    for(Function cf: f.getCallingFunctions(monitor)) out.append(" caller "+cf.getEntryPoint()+" "+cf.getName()+"\n"); break; }
                case "a": { Address t=A(p[0]); int n=p.length>1?Integer.parseInt(p[1].trim()):20;
                    for(Reference r: getReferencesTo(t)) { if(!r.getReferenceType().isCall()) continue;
                        Function f=getFunctionContaining(r.getFromAddress());
                        out.append("-- call at "+r.getFromAddress()+" in "+(f==null?"-":f.getEntryPoint().toString())+"\n");
                        Instruction ins=getInstructionAt(r.getFromAddress()); Deque<Instruction> q=new ArrayDeque<>();
                        Instruction pr=ins; for(int j=0;j<n&&pr!=null;j++){ pr=pr.getPrevious(); if(pr!=null) q.addFirst(pr);}
                        for(Instruction x: q) out.append("   "+x.getAddress()+"  "+x+"\n");
                        out.append(" > "+ins.getAddress()+"  "+ins+"\n"); } break; }
                case "v": { Address base=A(p[0]); int n=p.length>1?Integer.parseInt(p[1].trim()):24;
                    for(int j=0;j<n;j++){ long q=currentProgram.getMemory().getLong(base.add(j*8)); Function f=null; try{ f=getFunctionAt(toAddr(q)); }catch(Exception e){}
                        out.append(String.format("  +%x: %x%s\n",j*8,q,f==null?"":(" fn size="+f.getBody().getNumAddresses()))); } break; }
                case "s": { Address st=currentProgram.getMinAddress(); byte[] pat=v.getBytes(); int n=0;
                    while(n<6){ Address f=currentProgram.getMemory().findBytes(st,pat,null,true,monitor); if(f==null)break; out.append("found @"+f+"\n"); xrefs(f); st=f.add(1); n++; } break; }
                case "k": { String[] r=v.split("-"); long lo=Long.decode(r[0]), hi=Long.decode(r[1]); int n=0;
                    for(Instruction ins: currentProgram.getListing().getInstructions(true)){ for(int o=0;o<ins.getNumOperands();o++) for(Object ob: ins.getOpObjects(o))
                        if(ob instanceof ghidra.program.model.scalar.Scalar){ long sv=((ghidra.program.model.scalar.Scalar)ob).getUnsignedValue();
                            if(sv>=lo&&sv<=hi&&n<400){ Function f=getFunctionContaining(ins.getAddress()); out.append(ins.getAddress()+" "+(f==null?"-":f.getEntryPoint().toString())+" "+ins+"\n"); n++; } } } break; }
                case "n": { Address a0=A(p[0]); for(int j=0;j<0x200;j++){ Address a1=a0.subtract(j*8); Symbol sy=currentProgram.getSymbolTable().getPrimarySymbol(a1);
                    if(sy!=null){ out.append("-"+Integer.toHexString(j*8)+" "+a1+" "+sy.getName(true)+"\n"); break; } } break; }
                case "f": { Address a0=A(p[0]); int n=p.length>1?Integer.parseInt(p[1].trim()):8; for(int j=0;j<n;j++) out.append(String.format("%s+%x: %g\n",a0,j*4,Float.intBitsToFloat(currentProgram.getMemory().getInt(a0.add(j*4))))); break; }
                case "g": { java.util.regex.Pattern re=java.util.regex.Pattern.compile(v); int n=0;
                    for(Instruction ins: currentProgram.getListing().getInstructions(true)){ if(n>=400) break; String tx=ins.toString();
                        if(re.matcher(tx).find()){ Function f=getFunctionContaining(ins.getAddress()); out.append(ins.getAddress()+" "+(f==null?"-":f.getEntryPoint().toString())+" "+tx+"\n"); n++; } } break; }
                case "w": { int n=0; for(Symbol sy: currentProgram.getSymbolTable().getAllSymbols(false)){ String nm=sy.getName(true);
                    if(nm.contains(v)){ out.append(sy.getAddress()+" "+nm+"\n"); if(++n>=80) break; } } break; }
                default: out.append("unknown op\n");
            } } catch(Exception e) { out.append("ERR "+e+"\n"); }
        }
        Files.writeString(Path.of(a[0]),out.toString());
    }
    void xrefs(Address ad){ int n=0; for(Reference r: getReferencesTo(ad)){ if(++n>80){out.append(" ...\n");break;} Function f=getFunctionContaining(r.getFromAddress());
        out.append(" ref "+r.getFromAddress()+" "+r.getReferenceType()+" in "+(f==null?"-":f.getEntryPoint()+" "+f.getName())+"\n"); } }
    void decomp(Address ad){ Function f=getFunctionContaining(ad); if(f==null){ out.append("nofunc\n"); return; }
        DecompileResults r=di.decompileFunction(f,180,monitor);
        out.append("// "+f.getName()+" @"+f.getEntryPoint()+"\n");
        out.append(r.decompileCompleted()? r.getDecompiledFunction().getC(): "FAIL "+r.getErrorMessage()).append("\n"); }
}
