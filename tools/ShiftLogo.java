import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import java.io.*;

/**
 * Shifts the logo placement in ComponentLogo SWF files by adjusting
 * the Y-translate of the PlaceObject2 tag.
 *
 * Usage: ShiftLogo <input.swf> <output.swf> <yShiftPixels>
 *
 * A negative yShiftPixels moves the logo UP, positive moves it DOWN.
 * The shift is scaled proportionally for non-1080p variants based on
 * the SWF's frame height relative to 1080.
 */
public class ShiftLogo {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: ShiftLogo <input.swf> <output.swf> <yShiftPixels>");
            System.exit(1);
        }

        String inputPath = args[0];
        String outputPath = args[1];
        int yShiftPx1080 = Integer.parseInt(args[2]);

        System.out.println("Loading: " + inputPath);
        SWF swf = new SWF(new FileInputStream(inputPath), false);

        // Frame height in twips -> pixels to compute scale ratio vs 1080p
        int frameHeightTwips = swf.displayRect.Ymax - swf.displayRect.Ymin;
        double frameHeightPx = frameHeightTwips / 20.0;
        double ratio = frameHeightPx / 1080.0;
        int yShiftTwips = (int) Math.round(yShiftPx1080 * ratio * 20);

        System.out.println("Frame height: " + frameHeightPx + "px, ratio: " + ratio);
        System.out.println("Y shift: " + yShiftPx1080 + "px@1080 -> " + (yShiftTwips / 20.0) + "px actual (" + yShiftTwips + " twips)");

        int modified = 0;
        for (Tag tag : swf.getTags()) {
            // Handle PlaceObject2 at top level
            if (tag instanceof PlaceObject2Tag) {
                PlaceObject2Tag po = (PlaceObject2Tag) tag;
                if (po.getMatrix() != null) {
                    MATRIX m = po.getMatrix();
                    int oldY = m.translateY;
                    m.translateY += yShiftTwips;
                    System.out.println("PlaceObject2 '" + po.getInstanceName() + "': translateY " + oldY + " -> " + m.translateY + " twips (" + (oldY/20.0) + " -> " + (m.translateY/20.0) + " px)");
                    po.setModified(true);
                    modified++;
                }
            }
            // Handle PlaceObject3 at top level (used by 480 and Split720)
            if (tag instanceof PlaceObject3Tag) {
                PlaceObject3Tag po = (PlaceObject3Tag) tag;
                if (po.getMatrix() != null) {
                    MATRIX m = po.getMatrix();
                    int oldY = m.translateY;
                    m.translateY += yShiftTwips;
                    System.out.println("PlaceObject3 '" + po.getInstanceName() + "': translateY " + oldY + " -> " + m.translateY + " twips (" + (oldY/20.0) + " -> " + (m.translateY/20.0) + " px)");
                    po.setModified(true);
                    modified++;
                }
            }
            // Handle PlaceObject3 inside DefineSprite
            if (tag instanceof DefineSpriteTag) {
                DefineSpriteTag sprite = (DefineSpriteTag) tag;
                for (Tag sub : sprite.getTags()) {
                    if (sub instanceof PlaceObject3Tag) {
                        PlaceObject3Tag po = (PlaceObject3Tag) sub;
                        if (po.getMatrix() != null) {
                            MATRIX m = po.getMatrix();
                            int oldY = m.translateY;
                            m.translateY += yShiftTwips;
                            System.out.println("PlaceObject3 (in sprite): translateY " + oldY + " -> " + m.translateY + " twips (" + (oldY/20.0) + " -> " + (m.translateY/20.0) + " px)");
                            po.setModified(true);
                            modified++;
                        }
                    }
                }
            }
        }

        System.out.println("Modified " + modified + " placement tags");
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Saved: " + outputPath);
    }
}
