import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.awt.*;
import java.io.*;
import java.util.*;

/**
 * Replaces MenuTitle and MenuTitleSmall bitmaps in a skin SWF
 * with a custom logo image, then saves the modified SWF.
 *
 * Usage: ReplaceLogo <skin.swf> <logo.png> <output.swf> [scale]
 *
 * Optional scale parameter (0.0-1.0) shrinks the logo within the bitmap
 * canvas, adding transparent padding. Default is 1.0 (fill canvas).
 *
 * Finds bitmaps by symbol name (MenuTitle, MenuTitleSmall) rather than
 * hardcoded IDs, so it works with both skinHDWin.swf and skinWin.swf.
 */
public class ReplaceLogo {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: ReplaceLogo <skin.swf> <logo.png> <output.swf>");
            System.exit(1);
        }

        String swfPath = args[0];
        String logoPath = args[1];
        String outputPath = args[2];
        double extraScale = (args.length >= 4) ? Double.parseDouble(args[3]) : 1.0;

        System.out.println("Loading SWF: " + swfPath);
        SWF swf = new SWF(new FileInputStream(swfPath), false);

        System.out.println("Loading logo: " + logoPath);
        BufferedImage logoOriginal = ImageIO.read(new File(logoPath));
        System.out.println("Logo dimensions: " + logoOriginal.getWidth() + "x" + logoOriginal.getHeight());
        System.out.println("Extra scale: " + extraScale);

        // Build symbol name map from SymbolClass tags
        Map<Integer, String> symbolNames = new HashMap<>();
        for (Tag tag : swf.getTags()) {
            if (tag instanceof SymbolClassTag) {
                SymbolClassTag sym = (SymbolClassTag) tag;
                for (int i = 0; i < sym.tags.size(); i++) {
                    symbolNames.put(sym.tags.get(i), sym.names.get(i));
                }
            }
        }

        // Find and replace MenuTitle / MenuTitleSmall bitmaps by symbol name
        Set<String> targetNames = Set.of("MenuTitle", "MenuTitleSmall");
        int replaced = 0;

        for (Tag tag : swf.getTags()) {
            if (tag instanceof DefineBitsLossless2Tag) {
                DefineBitsLossless2Tag bmp = (DefineBitsLossless2Tag) tag;
                String symName = symbolNames.getOrDefault(bmp.characterID, "");

                if (targetNames.contains(symName)) {
                    int targetW = bmp.bitmapWidth;
                    int targetH = bmp.bitmapHeight;

                    System.out.println("Replacing " + symName + " (id=" + bmp.characterID + ", " + targetW + "x" + targetH + ")");

                    // Scale logo to fit within target dimensions, maintaining aspect ratio
                    // Then apply extra scale factor to shrink within the canvas
                    int fitW = (int) Math.round(targetW * extraScale);
                    int fitH = (int) Math.round(targetH * extraScale);
                    BufferedImage scaled = scaleToFit(logoOriginal, fitW, fitH);

                    // Create target-sized image with transparency, center the scaled logo
                    BufferedImage target = new BufferedImage(targetW, targetH, BufferedImage.TYPE_INT_ARGB);
                    Graphics2D g = target.createGraphics();
                    g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC);
                    g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

                    int xOff = (targetW - scaled.getWidth()) / 2;
                    int yOff = (targetH - scaled.getHeight()) / 2;
                    g.drawImage(scaled, xOff, yOff, null);
                    g.dispose();

                    // Convert BufferedImage to PNG bytes for FFDec setImage()
                    ByteArrayOutputStream baos = new ByteArrayOutputStream();
                    ImageIO.write(target, "PNG", baos);
                    bmp.setImage(baos.toByteArray());

                    System.out.println("  Replaced with " + scaled.getWidth() + "x" + scaled.getHeight() + " (centered in " + targetW + "x" + targetH + ")");
                    replaced++;
                }
            }
        }

        if (replaced == 0) {
            System.err.println("ERROR: No logo bitmaps found to replace!");
            System.exit(1);
        }

        System.out.println("Saving modified SWF: " + outputPath);
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Done! Replaced " + replaced + " bitmaps.");
    }

    static BufferedImage scaleToFit(BufferedImage src, int maxW, int maxH) {
        double scaleX = (double) maxW / src.getWidth();
        double scaleY = (double) maxH / src.getHeight();
        double scale = Math.min(scaleX, scaleY);

        int newW = (int) Math.round(src.getWidth() * scale);
        int newH = (int) Math.round(src.getHeight() * scale);

        BufferedImage result = new BufferedImage(newW, newH, BufferedImage.TYPE_INT_ARGB);
        Graphics2D g = result.createGraphics();
        g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC);
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g.drawImage(src, 0, 0, newW, newH, null);
        g.dispose();
        return result;
    }
}
