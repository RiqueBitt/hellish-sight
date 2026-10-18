import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;

public class DumpSwfDetail {
    public static void main(String[] args) throws Exception {
        String path = args[0];
        SWF swf = new SWF(new FileInputStream(path), false);

        System.out.println("SWF: " + path);
        System.out.println("Frame size: " + swf.displayRect);
        System.out.println("Frame rate: " + swf.frameRate);
        System.out.println();

        for (Tag tag : swf.getTags()) {
            System.out.println(tag.getClass().getSimpleName() + " - " + tag);

            if (tag instanceof ImportAssets2Tag) {
                ImportAssets2Tag imp = (ImportAssets2Tag) tag;
                System.out.println("  URL: " + imp.url);
                // Dump all fields via reflection
                for (Field f : imp.getClass().getFields()) {
                    try {
                        System.out.println("  field " + f.getName() + " = " + f.get(imp));
                    } catch (Exception e) {}
                }
            }

            if (tag instanceof DefineBitsLossless2Tag) {
                DefineBitsLossless2Tag bmp = (DefineBitsLossless2Tag) tag;
                System.out.println("  Bitmap: id=" + bmp.characterID + " " + bmp.bitmapWidth + "x" + bmp.bitmapHeight);
            }

            if (tag instanceof SymbolClassTag) {
                SymbolClassTag sym = (SymbolClassTag) tag;
                for (int i = 0; i < sym.tags.size(); i++) {
                    System.out.println("  Symbol: id=" + sym.tags.get(i) + " name=" + sym.names.get(i));
                }
            }
        }
    }
}
