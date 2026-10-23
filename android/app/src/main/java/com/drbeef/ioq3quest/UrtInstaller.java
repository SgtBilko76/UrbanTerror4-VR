package com.drbeef.ioq3quest;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Downloads Urban Terror 4.3.4 from FrozenSand's official servers and unpacks its q3ut4 data.
 *
 * Plain Java (no Android classes) so it can be tested on a desktop. The zip is the official
 * UrbanTerror434_full.zip; its size and MD5 are the published ones, so a broken or foreign file
 * is never unpacked. The download resumes (HTTP Range) after an interruption.
 */
public class UrtInstaller {

	public static final String[] MIRRORS = {
			"https://cdn.urbanterror.info/urt/43/releases/zips/UrbanTerror434_full.zip",
			"https://mirror2.urbanterror.info/UrbanTerror434_full.zip",
	};
	public static final long ZIP_SIZE = 1472003870L;
	public static final String ZIP_MD5 = "9bf7f0092161391697d24f6b004a6c6b";
	/** Unpacked size of the q3ut4 files we keep (pk3s + readme), for the free-space check. */
	public static final long DATA_SIZE = 1480000000L;
	private static final String ZIP_PREFIX = "UrbanTerror43/q3ut4/";
	private static final String MARKER = ".urt-install-incomplete";

	public interface Listener {
		/** stage: 0 = download, 1 = check, 2 = unpack. done/total in bytes. */
		void onProgress(int stage, long done, long total);
		boolean isCancelled();
	}

	private final File baseDir;   // /sdcard/UrTQuest
	private final File gameDir;   // /sdcard/UrTQuest/q3ut4

	public UrtInstaller(File baseDir) {
		this.baseDir = baseDir;
		this.gameDir = new File(baseDir, "q3ut4");
	}

	public File zipFile() {
		return new File(baseDir, "UrbanTerror434_full.zip");
	}

	public File partFile() {
		return new File(baseDir, "UrbanTerror434_full.zip.part");
	}

	/** The engine needs UrT's qvm pak and the 21 data paks; an interrupted unpack doesn't count. */
	public boolean isInstalled() {
		if (new File(gameDir, MARKER).exists()) return false;
		if (!new File(gameDir, "zUrT43_qvm.pk3").isFile()) return false;
		for (int i = 1; i <= 21; i++) {
			if (!new File(gameDir, String.format("zUrT43_%03d.pk3", i)).isFile()) return false;
		}
		return true;
	}

	/** Bytes still needed on the storage: the rest of the download plus the unpacked data. */
	public long bytesNeeded() {
		long have = zipFile().exists() ? ZIP_SIZE : partFile().length();
		return (ZIP_SIZE - have) + DATA_SIZE + 50L * 1024 * 1024;
	}

	public void install(Listener l) throws IOException {
		baseDir.mkdirs();
		gameDir.mkdirs();
		File zip = zipFile();
		if (!zip.exists() || zip.length() != ZIP_SIZE) {
			download(l);
		}
		check(zip, l);
		unpack(zip, l);
		zip.delete();
	}

	private void download(Listener l) throws IOException {
		File part = partFile();
		IOException last = null;
		for (String mirror : MIRRORS) {
			try {
				downloadFrom(mirror, part, l);
				if (part.length() != ZIP_SIZE) {
					throw new IOException("Download incomplete: " + part.length() + " of " + ZIP_SIZE + " bytes");
				}
				if (!part.renameTo(zipFile())) {
					throw new IOException("Cannot rename " + part);
				}
				return;
			} catch (IOException e) {
				if (l.isCancelled()) throw e;
				last = e;
			}
		}
		throw last != null ? last : new IOException("No download mirror");
	}

	private void downloadFrom(String url, File part, Listener l) throws IOException {
		long have = part.exists() ? part.length() : 0;
		if (have > ZIP_SIZE) {
			part.delete();
			have = 0;
		}
		if (have == ZIP_SIZE) return;
		HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
		c.setConnectTimeout(20000);
		c.setReadTimeout(30000);
		c.setInstanceFollowRedirects(true);
		if (have > 0) c.setRequestProperty("Range", "bytes=" + have + "-");
		int code = c.getResponseCode();
		if (code == 200) {
			have = 0;   // server ignored the range: start over
		} else if (code != 206) {
			c.disconnect();
			throw new IOException("HTTP " + code + " from " + url);
		}
		try (InputStream in = new BufferedInputStream(c.getInputStream(), 1 << 16);
			 RandomAccessFile out = new RandomAccessFile(part, "rw")) {
			out.setLength(have);
			out.seek(have);
			byte[] buf = new byte[1 << 16];
			long lastReport = 0;
			int n;
			while ((n = in.read(buf)) > 0) {
				if (l.isCancelled()) throw new IOException("Cancelled");
				out.write(buf, 0, n);
				have += n;
				if (have - lastReport > (1 << 20)) {
					lastReport = have;
					l.onProgress(0, have, ZIP_SIZE);
				}
			}
			l.onProgress(0, have, ZIP_SIZE);
		} finally {
			c.disconnect();
		}
	}

	private void check(File zip, Listener l) throws IOException {
		try (InputStream in = new FileInputStream(zip)) {
			MessageDigest md = MessageDigest.getInstance("MD5");
			byte[] buf = new byte[1 << 16];
			long done = 0, lastReport = 0;
			int n;
			while ((n = in.read(buf)) > 0) {
				if (l.isCancelled()) throw new IOException("Cancelled");
				md.update(buf, 0, n);
				done += n;
				if (done - lastReport > (8 << 20)) {
					lastReport = done;
					l.onProgress(1, done, ZIP_SIZE);
				}
			}
			StringBuilder sb = new StringBuilder();
			for (byte b : md.digest()) sb.append(String.format("%02x", b));
			if (!ZIP_MD5.equals(sb.toString())) {
				zip.delete();   // corrupt: the next attempt downloads it again
				throw new IOException("The downloaded file is damaged (MD5 " + sb + "). Please try again.");
			}
		} catch (java.security.NoSuchAlgorithmException e) {
			throw new IOException(e);
		}
	}

	private void unpack(File zip, Listener l) throws IOException {
		File marker = new File(gameDir, MARKER);
		new FileOutputStream(marker).close();
		try (ZipFile zf = new ZipFile(zip)) {
			long total = 0;
			for (Enumeration<? extends ZipEntry> e = zf.entries(); e.hasMoreElements(); ) {
				ZipEntry ze = e.nextElement();
				if (wanted(ze)) total += ze.getSize();
			}
			long done = 0;
			byte[] buf = new byte[1 << 16];
			for (Enumeration<? extends ZipEntry> e = zf.entries(); e.hasMoreElements(); ) {
				ZipEntry ze = e.nextElement();
				if (!wanted(ze)) continue;
				String name = ze.getName().substring(ZIP_PREFIX.length());
				File dst = new File(gameDir, name);
				File tmp = new File(gameDir, name + ".tmp");
				try (InputStream in = zf.getInputStream(ze); OutputStream out = new FileOutputStream(tmp)) {
					int n;
					long lastReport = 0, fileDone = 0;
					while ((n = in.read(buf)) > 0) {
						if (l.isCancelled()) throw new IOException("Cancelled");
						out.write(buf, 0, n);
						fileDone += n;
						if (fileDone - lastReport > (8 << 20)) {
							lastReport = fileDone;
							l.onProgress(2, done + fileDone, total);
						}
					}
				}
				if (tmp.length() != ze.getSize()) {
					throw new IOException("Unpacking " + name + " failed");
				}
				dst.delete();
				if (!tmp.renameTo(dst)) throw new IOException("Cannot write " + dst);
				done += ze.getSize();
				l.onProgress(2, done, total);
			}
		}
		marker.delete();
	}

	/** Only q3ut4's pk3 files and the readme (license text); no PC programs, no example configs. */
	private static boolean wanted(ZipEntry ze) {
		String n = ze.getName();
		if (ze.isDirectory() || !n.startsWith(ZIP_PREFIX)) return false;
		String rest = n.substring(ZIP_PREFIX.length());
		if (rest.contains("/") || rest.contains("\\") || rest.contains("..")) return false;
		String lower = rest.toLowerCase();
		return lower.endsWith(".pk3") || lower.equals("readme43.txt");
	}

	/** Desktop test: java UrtInstaller <dir> */
	public static void main(String[] args) throws Exception {
		UrtInstaller u = new UrtInstaller(new File(args[0]));
		System.out.println("installed before: " + u.isInstalled() + ", needed " + u.bytesNeeded());
		u.install(new Listener() {
			long t = 0;
			public void onProgress(int stage, long done, long total) {
				long now = System.currentTimeMillis();
				if (now - t > 2000 || done == total) {
					t = now;
					System.out.println("stage " + stage + " " + done + "/" + total);
				}
			}
			public boolean isCancelled() { return false; }
		});
		System.out.println("installed after: " + u.isInstalled());
	}
}
