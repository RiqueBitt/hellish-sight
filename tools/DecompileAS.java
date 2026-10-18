import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.abc.*;
import com.jpexs.decompiler.flash.abc.avm2.AVM2ConstantPool;
import com.jpexs.decompiler.flash.abc.types.*;
import com.jpexs.decompiler.flash.abc.types.traits.*;
import java.io.*;
import java.util.*;

/**
 * Decompiles ActionScript 3 class/method signatures from a SWF.
 * Usage: DecompileAS <swf-file>
 */
public class DecompileAS {
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.out.println("Usage: DecompileAS <swf-file>");
            return;
        }

        SWF swf = new SWF(new FileInputStream(args[0]), false);

        for (Tag tag : swf.getTags()) {
            if (tag instanceof ImportAssets2Tag) {
                ImportAssets2Tag imp = (ImportAssets2Tag) tag;
                System.out.println("=== ImportAssets2 ===");
                System.out.println("  URL: " + imp.url);
            }

            if (tag instanceof DoABC2Tag) {
                DoABC2Tag abcTag = (DoABC2Tag) tag;
                ABC abc = abcTag.getABC();
                AVM2ConstantPool cp = abc.constants;

                System.out.println("=== ABC: " + abcTag.name + " ===");
                System.out.println("Classes: " + abc.instance_info.size());
                System.out.println();

                for (int c = 0; c < abc.instance_info.size(); c++) {
                    InstanceInfo ii = abc.instance_info.get(c);
                    System.out.println("--- Class: " + resolveName(cp, ii.name_index) + " ---");
                    if (ii.super_index > 0)
                        System.out.println("  extends " + resolveName(cp, ii.super_index));

                    for (Trait t : ii.instance_traits.traits) {
                        dumpTrait(abc, cp, t, "  ");
                    }

                    ClassInfo ci = abc.class_info.get(c);
                    for (Trait t : ci.static_traits.traits) {
                        System.out.print("  [static] ");
                        dumpTrait(abc, cp, t, "");
                    }
                    System.out.println();
                }
            }
        }
    }

    static String resolveName(AVM2ConstantPool cp, int multinameIdx) {
        if (multinameIdx <= 0) return "*";
        Multiname mn = cp.getMultiname(multinameIdx);
        String name = mn.name_index > 0 ? cp.getString(mn.name_index) : "?";
        int nsIdx = mn.namespace_index;
        if (nsIdx > 0) {
            Namespace ns = cp.getNamespace(nsIdx);
            String nsName = ns.name_index > 0 ? cp.getString(ns.name_index) : "";
            if (!nsName.isEmpty()) return nsName + "." + name;
        }
        return name;
    }

    static void dumpTrait(ABC abc, AVM2ConstantPool cp, Trait t, String indent) {
        try {
            String name = resolveName(cp, t.name_index);

            if (t instanceof TraitMethodGetterSetter) {
                TraitMethodGetterSetter tm = (TraitMethodGetterSetter) t;
                MethodInfo mi = abc.method_info.get(tm.method_info);

                String kind = tm.kindType == Trait.TRAIT_GETTER ? "get" :
                              tm.kindType == Trait.TRAIT_SETTER ? "set" : "function";

                StringBuilder sig = new StringBuilder();
                sig.append(kind).append(" ").append(name).append("(");
                for (int p = 0; p < mi.param_types.length; p++) {
                    if (p > 0) sig.append(", ");
                    sig.append(resolveName(cp, mi.param_types[p]));
                }
                sig.append(")");
                sig.append(" : ").append(resolveName(cp, mi.ret_type));

                System.out.println(indent + sig);
            } else if (t instanceof TraitSlotConst) {
                TraitSlotConst ts = (TraitSlotConst) t;
                String type = resolveName(cp, ts.type_index);
                String constKind = ts.kindType == Trait.TRAIT_CONST ? "const" : "var";
                System.out.println(indent + constKind + " " + name + " : " + type);
            } else if (t instanceof TraitClass) {
                System.out.println(indent + "[class] " + name);
            }
        } catch (Exception e) {
            System.out.println(indent + "[error: " + e.getMessage() + "]");
        }
    }
}
