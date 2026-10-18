import com.jpexs.decompiler.flash.tags.*;
import java.lang.reflect.*;

public class CheckSetImage {
    public static void main(String[] args) {
        for (Method m : DefineBitsLossless2Tag.class.getMethods()) {
            if (m.getName().contains("mage") || m.getName().contains("itmap") || m.getName().contains("set")) {
                System.out.println(m.getReturnType().getSimpleName() + " " + m.getName() + "(" + java.util.Arrays.toString(m.getParameterTypes()) + ")");
            }
        }
    }
}
