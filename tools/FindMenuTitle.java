import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import java.io.*;

public class FindMenuTitle {
    public static void main(String[] args) throws Exception {
        String path = args[0];
        System.out.println("Scanning: " + path);
        SWF swf = new SWF(new FileInputStream(path), false);

        for (Tag tag : swf.getTags()) {
            if (tag instanceof SymbolClassTag) {
                SymbolClassTag sym = (SymbolClassTag) tag;
                for (int i = 0; i < sym.tags.size(); i++) {
                    String name = sym.names.get(i);
                    if (name.contains("Menu") || name.contains("Logo") || name.contains("Title")) {
                        System.out.println("  Symbol: id=" + sym.tags.get(i) + " name=" + name);
                    }
                }
            }
            if (tag instanceof ExportAssetsTag) {
                ExportAssetsTag exp = (ExportAssetsTag) tag;
                for (int i = 0; i < exp.tags.size(); i++) {
                    String name = exp.names.get(i);
                    if (name.contains("Menu") || name.contains("Logo") || name.contains("Title")) {
                        System.out.println("  Export: id=" + exp.tags.get(i) + " name=" + name);
                    }
                }
            }
        }
    }
}
