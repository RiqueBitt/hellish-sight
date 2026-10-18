import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.Tag;
import com.jpexs.decompiler.flash.tags.base.PlaceObjectTypeTag;
import com.jpexs.decompiler.flash.types.MATRIX;
import java.io.*;

/**
 * Shifts a named PlaceObject element's Y position in a SWF.
 * Usage: ShiftMenuY <input.swf> <output.swf> <element_name> <y_offset_pixels>
 *
 * Positive offset = move down, negative = move up.
 * SWF uses twips (1 pixel = 20 twips).
 */
public class ShiftMenuY {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: ShiftMenuY <input.swf> <output.swf> <y_offset_pixels> [element_name]");
            System.out.println("  Shifts all elements (or a named element) by the given pixel offset.");
            System.out.println("  Negative values move up, positive move down.");
            return;
        }

        String inputPath = args[0];
        String outputPath = args[1];
        int offsetPixels = Integer.parseInt(args[2]);
        String targetName = args.length > 3 ? args[3] : null;
        int offsetTwips = offsetPixels * 20;

        SWF swf = new SWF(new FileInputStream(inputPath), false);

        int modified = 0;
        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObjectTypeTag) {
                PlaceObjectTypeTag po = (PlaceObjectTypeTag) tag;
                String name = po.getInstanceName();
                if (targetName != null && !targetName.equals(name)) continue;
                MATRIX matrix = po.getMatrix();
                if (matrix != null) {
                    int oldY = matrix.translateY;
                    matrix.translateY += offsetTwips;
                    System.out.printf("  %s: Y %d -> %d twips (%+d px)%n",
                        name != null ? name : "(unnamed)",
                        oldY, matrix.translateY, offsetPixels);
                    po.setModified(true);
                    modified++;
                }
            }
        }

        if (modified == 0) {
            System.out.println("No elements modified.");
            return;
        }
        System.out.printf("Modified %d element(s) by %+d pixels%n", modified, offsetPixels);

        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Saved: " + outputPath);
    }
}
