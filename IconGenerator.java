import javax.imageio.ImageIO;
import java.awt.Graphics2D;
import java.awt.Image;
import java.awt.image.BufferedImage;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;

public class IconGenerator {
    public static void main(String[] args) {
        try {
            URL url = new URL("https://neeko-copilot.bytedance.net/api/text_to_image?prompt=cute%20anime%20girl%20pink%20hair%20kawaii%20chibi%20style%20white%20background&image_size=square");
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(30000);
            conn.setReadTimeout(30000);
            
            BufferedImage originalImage = ImageIO.read(conn.getInputStream());
            
            String[] densities = {"mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi"};
            int[] sizes = {48, 72, 96, 144, 192};
            
            for (int i = 0; i < densities.length; i++) {
                int size = sizes[i];
                String density = densities[i];
                
                BufferedImage resizedImage = new BufferedImage(size, size, BufferedImage.TYPE_INT_ARGB);
                Graphics2D g = resizedImage.createGraphics();
                g.drawImage(originalImage.getScaledInstance(size, size, Image.SCALE_SMOOTH), 0, 0, null);
                g.dispose();
                
                String dir = "app/src/main/res/mipmap-" + density;
                File dirFile = new File(dir);
                if (!dirFile.exists()) {
                    dirFile.mkdirs();
                }
                
                FileOutputStream fos = new FileOutputStream(dir + "/ic_launcher.webp");
                ImageIO.write(resizedImage, "webp", fos);
                fos.close();
                
                FileOutputStream fosRound = new FileOutputStream(dir + "/ic_launcher_round.webp");
                ImageIO.write(resizedImage, "webp", fosRound);
                fosRound.close();
                
                System.out.println("Generated: " + density + " (" + size + "x" + size + ")");
            }
            
            System.out.println("Done!");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}