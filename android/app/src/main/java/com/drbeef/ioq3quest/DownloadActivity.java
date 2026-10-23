package com.drbeef.ioq3quest;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.text.method.LinkMovementMethod;
import android.text.Html;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;

/**
 * First start without the Urban Terror data: a plain 2D panel (immersive activities can't show
 * Android views on the headset) that downloads Urban Terror 4.3.4 from FrozenSand's official
 * servers, checks it and unpacks the q3ut4 data. Then it starts the game.
 */
public class DownloadActivity extends Activity {

	private static final String TAG = "ioquake3Quest";
	public static final File BASE_DIR = new File("/sdcard/UrTQuest");

	private TextView status;
	private ProgressBar progress;
	private Button agree;
	private Button quit;
	private final Handler ui = new Handler(Looper.getMainLooper());
	private volatile boolean cancelled = false;
	private Thread worker;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		int pad = dp(28);
		LinearLayout root = new LinearLayout(this);
		root.setOrientation(LinearLayout.VERTICAL);
		root.setPadding(pad, pad, pad, pad);
		root.setBackgroundColor(Color.rgb(14, 30, 60));

		TextView title = new TextView(this);
		title.setText("UrbanTerror4-VR");
		title.setTextColor(Color.WHITE);
		title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);
		root.addView(title);

		TextView info = new TextView(this);
		info.setTextColor(Color.rgb(220, 225, 235));
		info.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
		info.setLineSpacing(0, 1.2f);
		info.setText(Html.fromHtml(
				"This VR port needs the free Urban Terror 4.3.4 game data (1.4 GB download, "
				+ "about 3 GB free space needed while installing).<br><br>"
				+ "It is downloaded from FrozenSand's official server "
				+ "(<a href=\"https://www.urbanterror.info/\">urbanterror.info</a>) and installed to "
				+ "/sdcard/UrTQuest/q3ut4. Urban Terror is made and owned by FrozenSand; by "
				+ "downloading you accept its terms (readme43.txt, installed with the game).<br><br>"
				+ "Already have Urban Terror 4.3? Quit, copy its q3ut4 folder to "
				+ "/sdcard/UrTQuest/q3ut4 and start the app again."));
		info.setMovementMethod(LinkMovementMethod.getInstance());
		info.setLinkTextColor(Color.rgb(110, 170, 255));
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
		lp.topMargin = dp(16);
		root.addView(info, lp);

		progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
		progress.setMax(1000);
		progress.setVisibility(View.INVISIBLE);
		LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT, dp(24));
		pp.topMargin = dp(24);
		root.addView(progress, pp);

		status = new TextView(this);
		status.setTextColor(Color.WHITE);
		status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
		LinearLayout.LayoutParams sp = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
		sp.topMargin = dp(10);
		root.addView(status, sp);

		LinearLayout buttons = new LinearLayout(this);
		buttons.setOrientation(LinearLayout.HORIZONTAL);
		buttons.setGravity(Gravity.CENTER_HORIZONTAL);
		agree = new Button(this);
		agree.setText("I agree - download and install");
		agree.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
		agree.setOnClickListener(v -> startInstall());
		quit = new Button(this);
		quit.setText("Quit");
		quit.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
		quit.setOnClickListener(v -> {
			cancelled = true;
			finishAffinity();
		});
		LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
		bp.setMargins(dp(12), 0, dp(12), 0);
		buttons.addView(agree, bp);
		buttons.addView(quit, bp);
		LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
		blp.topMargin = dp(24);
		root.addView(buttons, blp);

		setContentView(root);

		UrtInstaller inst = new UrtInstaller(BASE_DIR);
		if (inst.partFile().exists() || inst.zipFile().exists()) {
			status.setText("An unfinished download was found; it will continue where it stopped.");
		}
	}

	private int dp(int v) {
		return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
	}

	private void startInstall() {
		if (worker != null) return;
		final UrtInstaller inst = new UrtInstaller(BASE_DIR);
		BASE_DIR.mkdirs();
		long free = new StatFs(BASE_DIR.getAbsolutePath()).getAvailableBytes();
		long need = inst.bytesNeeded();
		if (free < need) {
			status.setText(String.format("Not enough free space: %.1f GB needed, %.1f GB free.",
					need / 1e9, free / 1e9));
			return;
		}
		agree.setEnabled(false);
		progress.setVisibility(View.VISIBLE);
		status.setText("Connecting...");
		worker = new Thread(() -> {
			try {
				inst.install(new UrtInstaller.Listener() {
					@Override
					public void onProgress(int stage, long done, long total) {
						ui.post(() -> showProgress(stage, done, total));
					}

					@Override
					public boolean isCancelled() {
						return cancelled;
					}
				});
				ui.post(this::launchGame);
			} catch (Exception e) {
				Log.e(TAG, "Urban Terror install failed", e);
				if (!cancelled) {
					ui.post(() -> {
						status.setText("Failed: " + e.getMessage()
								+ "\nCheck the Wi-Fi and press the button again; the download continues where it stopped.");
						agree.setText("Try again");
						agree.setEnabled(true);
						worker = null;
					});
				}
			}
		}, "UrtInstaller");
		worker.start();
	}

	private void showProgress(int stage, long done, long total) {
		progress.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
		String[] what = {"Downloading", "Checking the download", "Installing"};
		status.setText(String.format("%s: %d of %d MB", what[stage], done >> 20, total >> 20));
	}

	private void launchGame() {
		Intent intent = new Intent(this, MainActivity.class);
		intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
		startActivity(intent);
		finish();
	}

	@Override
	protected void onDestroy() {
		cancelled = true;
		super.onDestroy();
	}
}
