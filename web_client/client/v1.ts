import { shorteningRequestV1, lookupRequestV1, lookupRequestWeb } from '../rpc';
import {
	encryptV1,
	decryptV1,
	deriveKeyV1,
	DeriveKeyV1Params,
	IV_BYTES_V1,
	PASS_BYTES_V1,
	SALT_BYTES_V1,
} from '../crypto';
import { fromByteArray, toByteArray, fromURLSafeBase64 } from '../util';
import {ec_prv as private_url} from './../fbs/private_url_generated';
import { flatbuffers }  from '../vendor/flatbuffers/flatbuffers';

export class ShortenerClient {
	private iv_: Uint8Array = new Uint8Array(IV_BYTES_V1);
	private pass_: Uint8Array = new Uint8Array(PASS_BYTES_V1);
	private salt_: Uint8Array = new Uint8Array(SALT_BYTES_V1);
	private expiry_: Date = defaultExpiry();

	public constructor() {
		window.crypto.getRandomValues(this.iv_);
		window.crypto.getRandomValues(this.pass_);
		window.crypto.getRandomValues(this.salt_);
	}

	public async shorten(url: string) {
		const key = await deriveKeyV1({
			salt: this.salt_,
			iv: this.iv_,
			pass: this.pass_,
		});
		const m = await encryptV1({
			key,
			iv: this.iv_,
			url,
		});
		const identifier = await shorteningRequestV1(
			new Uint8Array(m),
			this.iv_,
			this.salt_,
			this.expiry_,
		);
		const passAsStr = fromByteArray(this.pass_);
		return /* https://prv.ec/ */ `${identifier}#${passAsStr}`;
	}

	public set expiry(d: Date) {
		this.expiry_ = d;
	}

	public get expiry() {
		return this.expiry_;
	}
}

export class LookupClient {
	private identifier_: string; // base-66 encoded string representing the URLIndex
	private pass_: Uint8Array;

	public constructor(identifier: string, pass: string) {
		this.identifier_ = identifier;
		if (this.identifier_.length > 10) {
			throw new Error();
		}
		this.pass_ = toByteArray(pass);
	}

	public async lookup(): Promise<string> {
		let r = await lookupRequestWeb(this.identifier_);
		if (r == null) {
			return '';
		}
		let pu = private_url.fbs.PrivateURL.getRootAsPrivateURL(
			new flatbuffers.ByteBuffer(new Uint8Array(r.data)),
		).unpack();
		const key = await deriveKeyV1({
			salt: new Uint8Array(pu.salt),
			iv: new Uint8Array(pu.iv),
			pass: this.pass_,
		});
		const p = await decryptV1({
			key,
			iv: new Uint8Array(pu.iv),
			ciphertext: new Uint8Array(pu.blindedUrl),
		});
		return new TextDecoder('utf-8').decode(p);
	}
}

function defaultExpiry(): Date {
	return new Date(
		Math.floor(new Date().getTime() / 1000) + 60 * 60 * 24 * 364,
	);
}
