import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.*;
import java.util.*;

/**
 * Adds hardcore heart bitmap assets to a skinGraphicsHud SWF.
 * These bitmaps are referenced by class name from PlaceObject3 tags in
 * the health sprite frames defined in the parent skinHud/skinHDHud SWF.
 *
 * Usage: AddHardcoreBitmaps <skinGraphicsHud.swf> <textures-dir> <output.swf>
 */
public class AddHardcoreBitmaps {
    static final String[] TEXTURE_NAMES = {
        "Health_Background_Hardcore",
        "Health_Background_Hardcore_Flash",
        "Health_Full_Hardcore",
        "Health_Half_Hardcore",
        "Health_Full_Flash_Hardcore",
        "Health_Half_Flash_Hardcore",
        "Health_Full_Poison_Hardcore",
        "Health_Half_Poison_Hardcore",
        "Health_Full_Poison_Flash_Hardcore",
        "Health_Half_Poison_Flash_Hardcore"
    };

    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: AddHardcoreBitmaps <skinGraphicsHud.swf> <textures-dir> <output.swf>");
            System.exit(1);
        }

        String swfPath = args[0];
        String texturesDir = args[1];
        String outputPath = args[2];

        System.out.println("Loading SWF: " + swfPath);
        SWF swf = new SWF(new FileInputStream(swfPath), false);

        // Find max character ID
        int maxCharId = 0;
        for (Tag tag : swf.getTags()) {
            if (tag instanceof CharacterTag) {
                int id = ((CharacterTag) tag).getCharacterId();
                if (id > maxCharId) maxCharId = id;
            }
        }
        System.out.println("Max existing character ID: " + maxCharId);

        // Find SymbolClass tag
        SymbolClassTag symbolClass = null;
        int symbolClassIdx = -1;
        for (int i = 0; i < swf.getTags().size(); i++) {
            if (swf.getTags().get(i) instanceof SymbolClassTag) {
                symbolClass = (SymbolClassTag) swf.getTags().get(i);
                symbolClassIdx = i;
                break;
            }
        }

        if (symbolClass == null) {
            System.err.println("ERROR: No SymbolClass tag found");
            System.exit(1);
        }

        int nextCharId = maxCharId + 1;
        int added = 0;

        for (String texName : TEXTURE_NAMES) {
            String pngFile = texturesDir + "/" + texName + ".png";
            File f = new File(pngFile);
            if (!f.exists()) {
                System.err.println("ERROR: Missing texture: " + pngFile);
                System.exit(1);
            }

            BufferedImage img = ImageIO.read(f);
            System.out.println("  Adding " + texName + " (id=" + nextCharId + ", " + img.getWidth() + "x" + img.getHeight() + ")");

            // Create DefineBitsLossless2 tag
            DefineBitsLossless2Tag bmp = new DefineBitsLossless2Tag(swf);
            bmp.characterID = nextCharId;

            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            ImageIO.write(img, "PNG", baos);
            bmp.setImage(baos.toByteArray());

            // Insert before SymbolClass
            swf.addTag(symbolClassIdx, bmp);
            symbolClassIdx++; // SymbolClass shifted by 1

            // Add to SymbolClass
            symbolClass.tags.add(nextCharId);
            symbolClass.names.add(texName);

            nextCharId++;
            added++;
        }

        System.out.println("Added " + added + " hardcore heart bitmaps");

        // Mark SymbolClass as modified so new entries get serialized
        symbolClass.setModified(true);

        // Save
        System.out.println("Saving to: " + outputPath);
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Done!");
    }
}
