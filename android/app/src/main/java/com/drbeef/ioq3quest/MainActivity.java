package com.drbeef.ioq3quest;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.RemoteException;
import android.util.Log;
import android.util.Pair;
import android.view.KeyEvent;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.drbeef.externalhapticsservice.HapticServiceClient;
import com.drbeef.externalhapticsservice.HapticsConstants;

import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Vector;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import static android.system.Os.setenv;

public class MainActivity extends SDLActivity // implements KeyEvent.Callback
{
	private static final String SUPPORTED_ASCII = "qwertyuiop[]asdfghjkl;'\\<zxcvbnm,./QWERTYUIOP{}ASDFGHJKL:\"|>ZXCVBNM<>?`1234567890-=~!@#$%^&*()_+";
	private int permissionCount = 0;
	private static final int READ_EXTERNAL_STORAGE_PERMISSION_ID = 1;
	private static final int WRITE_EXTERNAL_STORAGE_PERMISSION_ID = 2;
	private static final String TAG = "ioquake3Quest";

	private boolean hapticsEnabled = false;

	String commandLineParams;

	private Vector<HapticServiceClient> externalHapticsServiceClients = new Vector<>();

	//Use a vector of pairs, it is possible a given package _could_ in the future support more than one haptic service
	//so a map here of Package -> Action would not work.
	private static Vector<Pair<String, String>> externalHapticsServiceDetails = new Vector<>();

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		Log.i(TAG, "onCreate called");
		// LauncherActivity has already secured storage access, so the assets can be deployed
		// straight away. This must happen before super.onCreate(), which starts the SDL thread
		// and with it the engine.
		try {
			create();
		} catch (Exception e) {
			Log.e(TAG, "Failed to deploy assets: " + e);
		}
		super.onCreate(savedInstanceState);

	}

	@Override protected void onDestroy()
	{
		Log.i(TAG, "onDestroy called");

		for (HapticServiceClient externalHapticsServiceClient : externalHapticsServiceClients) {
			externalHapticsServiceClient.stopBinding();
		}

		super.onDestroy();
	}

	@Override
	public boolean dispatchKeyEvent(KeyEvent event) {
		//ASCII characters directly passed into the engine
		if (SUPPORTED_ASCII.indexOf(event.getUnicodeChar()) >= 0) {
			nativeKey(event.getUnicodeChar(), event.getAction());
			return true;
		}
		//special keys using SDL
		return super.dispatchKeyEvent(event);
	}

	@Override
	public void onWindowFocusChanged(boolean hasFocus) {
		nativeFocusChanged(hasFocus);
	}

	public void create() throws IOException {
		// Prepare the Urban Terror game directory. The game data itself (the q3ut4
		// zUrT43_*.pk3 files) has to be copied there by the user.
		new File("/sdcard/UrTQuest/q3ut4").mkdirs();

		// Copy the command line params file and autoexec
		copy_asset("/sdcard/UrTQuest", "commandline.txt", false);
		copy_asset("/sdcard/UrTQuest/q3ut4", "autoexec.cfg", false);
		// VR GLSL shaders (r_externalGLSL 1 loads them from this pak)
		copy_asset("/sdcard/UrTQuest/q3ut4", "pakQ3Q.pk3", true);
		// Cleanup incompatible shaders
		delete_asset("/sdcard/UrTQuest/q3ut4/glsl");

		//Read these from a file and pass through
		commandLineParams = new String();

		//See if user is trying to use command line params
		if (new File("/sdcard/UrTQuest/commandline.txt").exists()) {
			BufferedReader br;
			try {
				br = new BufferedReader(new FileReader("/sdcard/UrTQuest/commandline.txt"));
				String s;
				StringBuilder sb = new StringBuilder(0);
				while ((s = br.readLine()) != null)
					sb.append(s + " ");
				br.close();

				commandLineParams = new String(sb.toString());
			} catch (FileNotFoundException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}

		Log.d(TAG, "setting env");
		try {
			//commandLineParams += " +map q3dm7";
			setenv("commandline", commandLineParams, true);
		} catch (Exception e) {
		}

		for (Pair<String, String> serviceDetail : externalHapticsServiceDetails) {
			HapticServiceClient client = new HapticServiceClient(this, (state, desc) -> {
				Log.v(TAG, "ExternalHapticsService " + serviceDetail.second + ": " + desc);
			}, new Intent(serviceDetail.second)
					.setPackage(serviceDetail.first));

			client.bindService();
			externalHapticsServiceClients.add(client);
		}

		Log.d(TAG, "nativeCreate");
		nativeCreate(this);
	}

	public void copy_asset(String path, String name, boolean force) {
		copy_asset(path, name, name, force);
	}

	public void copy_asset(String path, String name, String newName, boolean force) {
		File f = new File(path + "/" + newName);
		if (!f.exists() || force) {

			//Ensure we have an appropriate folder
			String fullname = path + "/" + name;
			String directory = fullname.substring(0, fullname.lastIndexOf("/"));
			new File(directory).mkdirs();
			_copy_asset(name, path + "/" + newName);
		}
	}

	public void delete_asset(String path) {
		File file = new File(path);
		delete_asset(file);
	}

	public void delete_asset(File file) {
		if (!file.exists()) {
			return;
		}
		if (file.isDirectory()) {
			for (File nestedFile : file.listFiles()) {
				delete_asset(nestedFile);
			}
		}
		file.delete();
	}

	public void _copy_asset(String name_in, String name_out) {
		AssetManager assets = this.getAssets();

		try {
			InputStream in = assets.open(name_in);
			OutputStream out = new FileOutputStream(name_out);

			copy_stream(in, out);

			out.close();
			in.close();

		} catch (Exception e) {

			e.printStackTrace();
		}

	}

	public static void copy_stream(InputStream in, OutputStream out)
			throws IOException {
		byte[] buf = new byte[1024];
		while (true) {
			int count = in.read(buf);
			if (count <= 0)
				break;
			out.write(buf, 0, count);
		}
	}

	public static native void nativeCreate(MainActivity thisObject);
	public static native void nativeFocusChanged(boolean focus);
	public static native void nativeKey(int keycode, int action);

	static {
		// Loaded first so a missing or wrong loader fails here rather than deep inside libmain.
		System.loadLibrary("openxr_loader");
		System.loadLibrary("main");

		//Add possible external haptic service details here
		externalHapticsServiceDetails.add(Pair.create(HapticsConstants.BHAPTICS_PACKAGE, HapticsConstants.BHAPTICS_ACTION_FILTER));
		externalHapticsServiceDetails.add(Pair.create(HapticsConstants.FORCETUBE_PACKAGE, HapticsConstants.FORCETUBE_ACTION_FILTER));
	}

	public void haptic_event(String event, int position, int flags, int intensity, float angle, float yHeight)  {

		boolean areHapticsEnabled = hapticsEnabled;
		for (HapticServiceClient externalHapticsServiceClient : externalHapticsServiceClients) {

			if (externalHapticsServiceClient.hasService()) {
				try {
					//Enabled all haptics services if required
					if (!areHapticsEnabled)
					{
						externalHapticsServiceClient.getHapticsService().hapticEnable();
						hapticsEnabled = true;
						continue;
					}

					if (event.compareTo("frame_tick") == 0)
					{
						externalHapticsServiceClient.getHapticsService().hapticFrameTick();
					}

					//Uses the Doom3Quest and RTCWQuest haptic patterns
					String app = "Doom3Quest";
					String eventID = event;
					if (event.contains(":"))
					{
						String[] items = event.split(":");
						app = items[0];
						eventID = items[1];
					}
					externalHapticsServiceClient.getHapticsService().hapticEvent(app, eventID, position, flags, intensity, angle, yHeight);
				}
				catch (RemoteException r)
				{
					Log.v(TAG, r.toString());
				}
			}
		}
	}
}
