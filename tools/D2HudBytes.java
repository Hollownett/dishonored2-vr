// Read-only: dump bytes (args: outfile, addr:len...)
// @category Dishonored2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import java.nio.file.*;
public class D2HudBytes extends GhidraScript {
    public void run() throws Exception {
        String[] a=getScriptArgs(); StringBuilder o=new StringBuilder();
        for(int i=1;i<a.length;i++){
            String[] p=a[i].split(":"); Address ad=toAddr(p[0]); int n=Integer.parseInt(p[1]);
            byte[] b=new byte[n]; currentProgram.getMemory().getBytes(ad,b);
            o.append(ad).append(":");
            for(int k=0;k<n;k++){ if(k%16==0) o.append("\n  "); o.append(String.format("%02x ",b[k]&255)); }
            o.append("\n");
        }
        Files.writeString(Path.of(a[0]),o.toString());
    }
}
