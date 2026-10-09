// Drive the display of a Namespace macOS machine over VNC for the release e2e gate.
//
// Usage: node display.mjs <target>
//   target: devbox:<name>, or instance:<id>:<ingress-domain> for an nsc instance
// Holds one VNC session open and reads one command per stdin line, answering
// each with one JSON line ({"ok":true,...} or {"ok":false,"error":...}).
// Reconnecting per action made macOS toggle its "screen is being controlled"
// state, which dropped clicks, keys, focus and open menus.
//   shot <file.png>       save the framebuffer (pixel coordinates, e.g. 2560x1600)
//   click <x> <y>         slow left click at framebuffer pixel coordinates
//   key <name>            press Return, Escape or Tab
//   rclick <x> <y>        slow right click (opens context menus)
//   sleep <ms>
//
// The Namespace SDK resolves the VNC endpoint and credentials from the local
// nsc/devbox login; neither is printed. The SDK client only offers screenshots
// and clicks, so pointer timing and key events are written as raw RFB messages.
// Text is pasted (context menu) rather than typed: typed keys were dropped over
// VNC, and the Command key's keysym differs between macOS versions.
import { writeFileSync } from "node:fs";
import { createInterface } from "node:readline";
import { cachingTokenSource, loadDefaults } from "@namespacelabs/sdk/auth";
import { createDevboxClient } from "@namespacelabs/sdk/devbox";

const [target] = process.argv.slice(2);
const KEYS = { Return: 0xff0d, Escape: 0xff1b, Tab: 0xff09, Shift: 0xffe1 };
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const keyEvent = (down, sym) => {
	const message = Buffer.alloc(8);
	message[0] = 4;
	message[1] = down ? 1 : 0;
	message.writeUInt32BE(sym, 4);
	return message;
};
const pointer = (mask, x, y) => {
	const message = Buffer.alloc(6);
	message[0] = 5;
	message[1] = mask;
	message.writeUInt16BE(x, 2);
	message.writeUInt16BE(y, 4);
	return message;
};

let display;
let close = () => {};
if (target.startsWith("devbox:")) {
	const devboxes = createDevboxClient();
	const handle = await devboxes.devboxes.get(target.slice("devbox:".length));
	// DevboxHandle.display has no key events; reuse its cached VNC session.
	display = await handle.connections.getDisplay(handle.id, { timeoutMs: 90000 });
	close = () => devboxes.close();
} else if (target.startsWith("instance:")) {
	// The SDK's display module is not a package export; it resolves an
	// instance's VNC endpoint and credentials from the regional Compute API.
	const { computeApiBaseUrl, createComputeClient, fetchVncConfig, openDisplay } = await import(
		new URL("./node_modules/@namespacelabs/sdk/dist/esm/devbox/display.js", import.meta.url));
	const [, instanceId, ingressDomain] = target.split(":");
	const tokens = cachingTokenSource(loadDefaults);
	const compute = createComputeClient(tokens, computeApiBaseUrl(ingressDomain));
	const config = await fetchVncConfig(compute, instanceId, { timeoutMs: 30000 });
	display = await openDisplay({
		instanceId, endpoint: config.endpoint, username: config.username, password: config.password,
		token: await tokens.issueToken(5 * 60 * 1000), signal: new AbortController().signal, timeoutMs: 60000,
	});
	close = () => display.close();
} else {
	throw new Error(`unknown target ${target}`);
}
const vnc = display.client;
const send = (messages) => vnc.run({ timeoutMs: 15000 }, async () => {
	for (const message of messages) await vnc.write(message);
});
const inBounds = (x, y) => {
	if (!(x >= 0 && y >= 0 && x < vnc.width && y < vnc.height)) throw new RangeError(`${x},${y} is outside ${vnc.width}x${vnc.height}`);
};

async function click(x, y, mask = 1) {
	inBounds(x, y);
	await send([pointer(0, Math.max(0, x - 5), Math.max(0, y - 5))]);
	await sleep(120);
	await send([pointer(0, x, y)]);
	await sleep(120);
	await send([pointer(mask, x, y)]);
	await sleep(150);
	await send([pointer(0, x, y)]);
}

async function run(command, args) {
	if (command === "shot") {
		const screenshot = await display.screenshot({ timeoutMs: 30000 });
		writeFileSync(args[0], screenshot.png);
		return { width: screenshot.width, height: screenshot.height };
	} else if (command === "click") {
		await click(Number(args[0]), Number(args[1]));
	} else if (command === "rclick") {
		await click(Number(args[0]), Number(args[1]), 4);
	} else if (command === "key") {
		const sym = KEYS[args[0]];
		if (!sym) throw new Error(`unknown key ${args[0]}`);
		await send([keyEvent(true, sym), keyEvent(false, sym)]);
	} else if (command === "sleep") {
		await sleep(Number(args[0]));
	} else {
		throw new Error(`unknown command ${command}`);
	}
	return {};
}

// The first input of a new session is consumed while macOS switches to
// "screen is being controlled" (a pointer move alone is not enough), so
// start with a harmless Shift press and let it settle.
await send([pointer(0, Math.floor(vnc.width / 2), Math.floor(vnc.height / 3))]);
await send([keyEvent(true, KEYS.Shift), keyEvent(false, KEYS.Shift)]);
await sleep(1500);
console.log(JSON.stringify({ ok: true, ready: true, width: vnc.width, height: vnc.height }));

const lines = createInterface({ input: process.stdin });
for await (const line of lines) {
	const [command, ...args] = line.trim().split(/\s+/);
	if (!command) continue;
	try {
		const result = await run(command, args);
		await sleep(150);
		console.log(JSON.stringify({ ok: true, ...result }));
	} catch (error) {
		console.log(JSON.stringify({ ok: false, error: String(error?.message ?? error) }));
	}
}
close();
process.exit(0);
