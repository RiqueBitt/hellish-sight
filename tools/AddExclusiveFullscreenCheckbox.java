import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import java.io.*;
import java.util.*;

/**
 * Adds an "ExclusiveFullscreen" checkbox to SettingsGraphicsMenu SWF files.
 * Clones the existing VSync checkbox, renames it "ExclusiveFullscreen",
 * positions it below VSync, and shifts sliders down.
 */
public class AddExclusiveFullscreenCheckbox {
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.out.println("Usage: AddExclusiveFullscreenCheckbox <swf_file> [output_file]");
            System.exit(1);
        }

        String inputPath = args[0];
        String outputPath = args.length > 1 ? args[1] : inputPath;

        SWF swf = new SWF(new FileInputStream(inputPath), false);

        // Check if ExclusiveFullscreen already exists
        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObject3Tag) {
                PlaceObject3Tag po = (PlaceObject3Tag) tag;
                if ("ExclusiveFullscreen".equals(po.name)) {
                    System.out.println("ExclusiveFullscreen checkbox already exists in " + inputPath + ", skipping.");
                    return;
                }
            }
        }

        // Find the VSync checkbox tag and all slider tags
        PlaceObject3Tag vsyncTag = null;
        PlaceObject3Tag customSkinAnimTag = null;
        List<PlaceObject3Tag> sliderTags = new ArrayList<>();
        int maxDepth = 0;

        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObject3Tag) {
                PlaceObject3Tag po = (PlaceObject3Tag) tag;
                if ("VSync".equals(po.name)) {
                    vsyncTag = po;
                }
                if ("CustomSkinAnim".equals(po.name)) {
                    customSkinAnimTag = po;
                }
                if (po.name != null && (po.name.equals("RenderDistance") || po.name.equals("Gamma")
                        || po.name.equals("FOV") || po.name.equals("InterfaceOpacity"))) {
                    sliderTags.add(po);
                }
                if (po.depth > maxDepth) maxDepth = po.depth;
            }
            if (tag instanceof PlaceObject2Tag) {
                PlaceObject2Tag po = (PlaceObject2Tag) tag;
                if (po.depth > maxDepth) maxDepth = po.depth;
            }
        }

        if (vsyncTag == null) {
            System.err.println("ERROR: Could not find VSync checkbox in " + inputPath);
            System.exit(1);
        }

        // Calculate checkbox Y-spacing from the gap between CustomSkinAnim and VSync
        int checkboxSpacing;
        if (customSkinAnimTag != null) {
            checkboxSpacing = vsyncTag.matrix.translateY - customSkinAnimTag.matrix.translateY;
        } else {
            checkboxSpacing = vsyncTag.matrix.translateY > 8000 ? 1080 : 720;
        }

        System.out.println("File: " + inputPath);
        System.out.println("  VSync Y: " + vsyncTag.matrix.translateY);
        System.out.println("  Checkbox spacing: " + checkboxSpacing);

        // Create the ExclusiveFullscreen PlaceObject3Tag by copying fields from VSync
        PlaceObject3Tag efTag = new PlaceObject3Tag(swf);
        efTag.placeFlagHasClassName = vsyncTag.placeFlagHasClassName;
        efTag.placeFlagHasName = true;
        efTag.placeFlagHasMatrix = true;
        efTag.placeFlagHasImage = vsyncTag.placeFlagHasImage;
        efTag.placeFlagHasCharacter = vsyncTag.placeFlagHasCharacter;
        efTag.placeFlagMove = vsyncTag.placeFlagMove;
        efTag.className = vsyncTag.className;
        efTag.name = "ExclusiveFullscreen";
        efTag.depth = maxDepth + 1;
        efTag.characterId = vsyncTag.characterId;

        // Copy and offset the matrix
        MATRIX m = new MATRIX(vsyncTag.matrix);
        m.translateY += checkboxSpacing;
        efTag.matrix = m;

        efTag.setModified(true);

        System.out.println("  ExclusiveFullscreen Y: " + m.translateY);
        System.out.println("  ExclusiveFullscreen depth: " + efTag.depth);

        // Shift all sliders down by checkboxSpacing
        for (PlaceObject3Tag slider : sliderTags) {
            System.out.println("  Shifting " + slider.name + " from Y=" + slider.matrix.translateY
                    + " to Y=" + (slider.matrix.translateY + checkboxSpacing));
            slider.matrix.translateY += checkboxSpacing;
            slider.setModified(true);
        }

        // Expand the background panel height
        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObject2Tag) {
                PlaceObject2Tag po = (PlaceObject2Tag) tag;
                if ("BackgroundPanel".equals(po.name)) {
                    int charId = po.characterId;
                    CharacterTag ct = swf.getCharacter(charId);
                    if (ct instanceof DefineSpriteTag) {
                        DefineSpriteTag sprite = (DefineSpriteTag) ct;
                        for (Tag sub : sprite.getTags()) {
                            if (sub instanceof PlaceObject3Tag) {
                                PlaceObject3Tag gridTag = (PlaceObject3Tag) sub;
                                float oldScaleY = gridTag.matrix.scaleY;
                                gridTag.matrix.scaleY += (float) checkboxSpacing / 640.0f;
                                gridTag.setModified(true);
                                System.out.println("  Background panel scaleY: " + oldScaleY + " -> " + gridTag.matrix.scaleY);
                            }
                        }
                    }
                }
            }
        }

        // Insert ExclusiveFullscreen tag right after VSync
        ArrayList<Tag> tagList = swf.getTags().toArrayList();
        int insertIdx = -1;
        for (int i = 0; i < tagList.size(); i++) {
            if (tagList.get(i) == vsyncTag) {
                insertIdx = i + 1;
                break;
            }
        }

        if (insertIdx >= 0) {
            swf.addTag(insertIdx, efTag);
            System.out.println("  Inserted ExclusiveFullscreen tag at index " + insertIdx);
        } else {
            System.err.println("ERROR: Could not find insertion point");
            System.exit(1);
        }

        // Save
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("  Saved to: " + outputPath);
    }
}
