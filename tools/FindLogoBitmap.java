import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import java.io.*;
import java.util.*;

public class FindLogoBitmap {
    public static void main(String[] args) throws Exception {
        SWF swf = new SWF(new FileInputStream(args[0]), false);

        // Find MenuTitle symbol
        Map<Integer, String> symbolMap = new HashMap<>();
        for (Tag tag : swf.getTags()) {
            if (tag instanceof SymbolClassTag) {
                SymbolClassTag sym = (SymbolClassTag) tag;
                for (int i = 0; i < sym.tags.size(); i++) {
                    symbolMap.put(sym.tags.get(i), sym.names.get(i));
                    if (sym.names.get(i).contains("Title") || sym.names.get(i).contains("Logo")) {
                        System.out.println("Symbol: id=" + sym.tags.get(i) + " name=" + sym.names.get(i));
                    }
                }
            }
            if (tag instanceof ExportAssetsTag) {
                ExportAssetsTag exp = (ExportAssetsTag) tag;
                for (int i = 0; i < exp.tags.size(); i++) {
                    if (exp.names.get(i).contains("Title") || exp.names.get(i).contains("Logo")) {
                        System.out.println("Export: id=" + exp.tags.get(i) + " name=" + exp.names.get(i));
                    }
                }
            }
        }

        // Find all bitmaps
        System.out.println("\n=== Bitmaps ===");
        for (Tag tag : swf.getTags()) {
            if (tag instanceof DefineBitsLossless2Tag) {
                DefineBitsLossless2Tag bmp = (DefineBitsLossless2Tag) tag;
                String sym = symbolMap.getOrDefault(bmp.characterID, "");
                System.out.println("Lossless2: id=" + bmp.characterID + " " + bmp.bitmapWidth + "x" + bmp.bitmapHeight + " sym=" + sym);
            }
            if (tag instanceof DefineBitsJPEG2Tag) {
                DefineBitsJPEG2Tag jpg = (DefineBitsJPEG2Tag) tag;
                String sym = symbolMap.getOrDefault(jpg.characterID, "");
                System.out.println("JPEG2: id=" + jpg.characterID + " sym=" + sym);
            }
            if (tag instanceof DefineBitsJPEG3Tag) {
                DefineBitsJPEG3Tag jpg = (DefineBitsJPEG3Tag) tag;
                String sym = symbolMap.getOrDefault(jpg.characterID, "");
                System.out.println("JPEG3: id=" + jpg.characterID + " sym=" + sym);
            }
            if (tag instanceof DefineBitsTag) {
                DefineBitsTag bmp = (DefineBitsTag) tag;
                String sym = symbolMap.getOrDefault(bmp.characterID, "");
                System.out.println("Bits: id=" + bmp.characterID + " sym=" + sym);
            }
            if (tag instanceof DefineBitsLosslessTag) {
                DefineBitsLosslessTag bmp = (DefineBitsLosslessTag) tag;
                String sym = symbolMap.getOrDefault(bmp.characterID, "");
                System.out.println("Lossless: id=" + bmp.characterID + " " + bmp.bitmapWidth + "x" + bmp.bitmapHeight + " sym=" + sym);
            }
        }
    }
}
