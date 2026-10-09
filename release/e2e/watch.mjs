// Open a Pixelview watch link in headless Chrome, screenshot the player and
// report whether video is actually playing.
//
// Usage: node watch.mjs <chrome-path> <link-file> <out.png> [wait-ms]
// The link carries the session password, so it is read from a 0600 file and
// never printed; only the page path is reported.
import { readFileSync } from "node:fs";
import puppeteer from "puppeteer-core";

const [chrome, linkFile, out, waitMs = "20000"] = process.argv.slice(2);
const link = readFileSync(linkFile, "utf8").trim();
const browser = await puppeteer.launch({
	executablePath: chrome,
	headless: true,
	args: ["--autoplay-policy=no-user-gesture-required", "--mute-audio", "--window-size=1440,900"],
	defaultViewport: { width: 1440, height: 900 },
});
const videos = (page) => page.evaluate(() => [...document.querySelectorAll("video")].map((video) => ({
	width: video.videoWidth,
	height: video.videoHeight,
	time: Number(video.currentTime.toFixed(2)),
	paused: video.paused,
	readyState: video.readyState,
	frames: video.getVideoPlaybackQuality?.().totalVideoFrames ?? null,
})));
try {
	const page = await browser.newPage();
	await page.goto(link, { waitUntil: "networkidle2", timeout: 60000 });
	// The player asks viewers for a display name before joining.
	const nameField = await page.$('input[placeholder="Name"]');
	if (nameField) {
		await nameField.type("Release e2e");
		await page.keyboard.press("Enter");
	}
	await new Promise((resolve) => setTimeout(resolve, Number(waitMs)));
	const before = await videos(page);
	await new Promise((resolve) => setTimeout(resolve, 3000));
	const after = await videos(page);
	await page.screenshot({ path: out });
	console.log(JSON.stringify({ path: new URL(page.url()).pathname, before, after }));
} finally {
	await browser.close();
}
