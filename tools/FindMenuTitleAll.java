import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import java.io.*;

public class FindMenuTitleAll {
    public static void main(String[] args) throws Exception {
        String[] files = {"skinHD.swf", "skinHDGraphics.swf", "skinHDWin.swf", "skinHDInGame.swf", "skinHDLabels.swf", "skinHDHud.swf", "skinHDGraphicsInGame.swf", "skinHDGraphicsLabels.swf", "skinHDGraphicsHud.swf", "skin.swf", "skinGraphics.swf", "skinWin.swf"};
        String base = "C:/Users/revela/Documents/Minecraft/itsRevela/Minecraft.Client/Common/Media/";
        
        for (String file : files) {
            try {
                SWF swf = new SWF(new FileInputStream(base + file), false);
                for (Tag tag : swf.getTags()) {
                    if (tag instanceof SymbolClassTag) {
                        SymbolClassTag sym = (SymbolClassTag) tag;
                        for (int i = 0; i < sym.tags.size(); i++) {
                            if (sym.names.get(i).contains("Title") || sym.names.get(i).contains("MenuTitle")) {
                                System.out.println(file + " -> Symbol: id=" + sym.tags.get(i) + " name=" + sym.names.get(i));
                            }
                        }
                    }
                    if (tag instanceof ExportAssetsTag) {
                        ExportAssetsTag exp = (ExportAssetsTag) tag;
                        for (int i = 0; i < exp.tags.size(); i++) {
                            if (exp.names.get(i).contains("Title") || exp.names.get(i).contains("MenuTitle")) {
                                System.out.println(file + " -> Export: id=" + exp.tags.get(i) + " name=" + exp.names.get(i));
                            }
                        }
                    }
                }
            } catch (Exception e) {
                System.err.println("Error reading " + file + ": " + e.getMessage());
            }
        }
    }
}
