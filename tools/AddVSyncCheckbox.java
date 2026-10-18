import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import java.io.*;
import java.util.*;

/**
 * Adds a "VSync" checkbox to SettingsGraphicsMenu SWF files.
 * Clones the existing CustomSkinAnim checkbox, renames it "VSync",
 * positions it below CustomSkinAnim, and shifts sliders down.
 */
public class AddVSyncCheckbox {
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.out.println("Usage: AddVSyncCheckbox <swf_file> [output_file]");
            System.exit(1);
        }

        String inputPath = args[0];
        String outputPath = args.length > 1 ? args[1] : inputPath;

        SWF swf = new SWF(new FileInputStream(inputPath), false);

        // Check if VSync already exists
        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObject3Tag) {
                PlaceObject3Tag po = (PlaceObject3Tag) tag;
                if ("VSync".equals(po.name)) {
                    System.out.println("VSync checkbox already exists in " + inputPath + ", skipping.");
                    return;
                }
            }
        }

        // Find the CustomSkinAnim checkbox tag and all slider tags
        PlaceObject3Tag customSkinAnimTag = null;
        PlaceObject3Tag bedrockFogTag = null;
        List<PlaceObject3Tag> sliderTags = new ArrayList<>();
        int maxDepth = 0;

        for (Tag tag : swf.getTags()) {
            if (tag instanceof PlaceObject3Tag) {
                PlaceObject3Tag po = (PlaceObject3Tag) tag;
                if ("CustomSkinAnim".equals(po.name)) {
                    customSkinAnimTag = po;
                }
                if ("BedrockFog".equals(po.name)) {
                    bedrockFogTag = po;
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

        if (customSkinAnimTag == null) {
            System.err.println("ERROR: Could not find CustomSkinAnim checkbox in " + inputPath);
            System.exit(1);
        }

        // Calculate checkbox Y-spacing
        int checkboxSpacing;
        if (bedrockFogTag != null) {
            checkboxSpacing = customSkinAnimTag.matrix.translateY - bedrockFogTag.matrix.translateY;
        } else {
            checkboxSpacing = customSkinAnimTag.matrix.translateY > 8000 ? 1080 : 720;
        }

        System.out.println("File: " + inputPath);
        System.out.println("  CustomSkinAnim Y: " + customSkinAnimTag.matrix.translateY);
        System.out.println("  Checkbox spacing: " + checkboxSpacing);

        // Create the VSync PlaceObject3Tag by copying fields from CustomSkinAnim
        PlaceObject3Tag vsyncTag = new PlaceObject3Tag(swf);
        vsyncTag.placeFlagHasClassName = customSkinAnimTag.placeFlagHasClassName;
        vsyncTag.placeFlagHasName = true;
        vsyncTag.placeFlagHasMatrix = true;
        vsyncTag.placeFlagHasImage = customSkinAnimTag.placeFlagHasImage;
        vsyncTag.placeFlagHasCharacter = customSkinAnimTag.placeFlagHasCharacter;
        vsyncTag.placeFlagMove = customSkinAnimTag.placeFlagMove;
        vsyncTag.className = customSkinAnimTag.className;
        vsyncTag.name = "VSync";
        vsyncTag.depth = maxDepth + 1;
        vsyncTag.characterId = customSkinAnimTag.characterId;

        // Copy and offset the matrix
        MATRIX m = new MATRIX(customSkinAnimTag.matrix);
        m.translateY += checkboxSpacing;
        vsyncTag.matrix = m;

        vsyncTag.setModified(true);

        System.out.println("  VSync Y: " + m.translateY);
        System.out.println("  VSync depth: " + vsyncTag.depth);
        System.out.println("  VSync className: " + vsyncTag.className);

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
                                // The 9-grid base tile is 640 twips (32px * 20 twips/px)
                                gridTag.matrix.scaleY += (float) checkboxSpacing / 640.0f;
                                gridTag.setModified(true);
                                System.out.println("  Background panel scaleY: " + oldScaleY + " -> " + gridTag.matrix.scaleY);
                            }
                        }
                    }
                }
            }
        }

        // Insert VSync tag right after CustomSkinAnim
        ArrayList<Tag> tagList = swf.getTags().toArrayList();
        int insertIdx = -1;
        for (int i = 0; i < tagList.size(); i++) {
            if (tagList.get(i) == customSkinAnimTag) {
                insertIdx = i + 1;
                break;
            }
        }

        if (insertIdx >= 0) {
            swf.addTag(insertIdx, vsyncTag);
            System.out.println("  Inserted VSync tag at index " + insertIdx);
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
