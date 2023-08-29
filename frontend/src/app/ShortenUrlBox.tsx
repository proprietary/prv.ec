'use client';

import React from 'react';
import ReCAPTCHA from 'react-google-recaptcha';

const urlShortenerBaseUrl = process.env.NEXT_PUBLIC_EC_PRV_URL_SHORTENER__BASE_URL;
const apiBaseUrl = process.env.NEXT_PUBLIC_EC_PRV_URL_SHORTENER__API_BASE_URL;

class UrlShortenerApiError extends Error {
    private statusCode: number;
    constructor(statusCode: number) {
        super(statusCode.toString());
        this.statusCode = statusCode;
        Object.setPrototypeOf(this, UrlShortenerApiError.prototype);
    }
    public getStatusCode(): number {
        return this.statusCode;
    }
}

const makeRequest = async (userCaptchaResponse: string, longUrl: string): Promise<string> => {
    const endpoint = `${apiBaseUrl}/api/v1/create`;
    const response = await fetch(endpoint, { method: 'POST', body: JSON.stringify({ 'user_captcha_response': userCaptchaResponse, 'long_url': longUrl }) });
    if (response.status == 200) {
        const responseBody = await response.text();
        return responseBody;
    } else {
        throw new UrlShortenerApiError(response.status);
    }
}

export default function ShortenUrlBox() {
    const [urlToShorten, setUrlToShorten] = React.useState<string>('');
    const [recaptchaUserResponse, setRecaptchaUserResponse] = React.useState<string>('');
    const [shortenedUrlResult, setShortenedUrlResult] = React.useState<string>('');
    const [didCopy, setDidCopy] = React.useState<boolean>(false);
    const [isLoading, setIsLoading] = React.useState<boolean>(false);
    const [errored, setErrored] = React.useState<boolean>(false);
    const [warned, setWarned] = React.useState<string>('');
    const recaptchaRef = React.useRef<ReCAPTCHA>();
    const handleChangeUrlToShorten = (e: React.ChangeEvent<HTMLInputElement>) => {
        e.preventDefault();
        setUrlToShorten(e.target.value);
    };
    const handleSubmit = async (e: React.FormEvent<HTMLFormElement>) => {
        e.preventDefault();
        setIsLoading(true);
        if (recaptchaRef != null && recaptchaRef.current != null) {
            recaptchaRef.current.reset();
        } else {
            console.error('reCAPTCHA not found');
            throw new Error();
        }
        try {
            const shortUrl = await makeRequest(recaptchaUserResponse, urlToShorten);
            setShortenedUrlResult(`${urlShortenerBaseUrl}/${shortUrl}`);
        } catch (e) {
            if (e instanceof UrlShortenerApiError) {
                if (e.getStatusCode() == 429) {
                    setWarned("You did that too much. Wait longer and try again later.");
                } else {
                    console.error(e);
                    setErrored(true);
                }
            } else {
                console.error(e);
                setErrored(true);
            }
        }
        setIsLoading(false);
    }
    const handleRecaptchaV2Change = (value: any) => {
        console.log(value);
        if (value == null) {
            setRecaptchaUserResponse('');
        } else {
            setRecaptchaUserResponse(value);
        }
    }
    const copyToClipboard = (e: any) => {
        e.preventDefault();
        e.stopPropagation();
        if (shortenedUrlResult.length > 0) {
            if ('clipboard' in navigator) {
                navigator.clipboard.writeText(shortenedUrlResult);
            } else {
                return document.execCommand('copy', true, shortenedUrlResult);
            }
            setDidCopy(true);
            setTimeout(() => { setDidCopy(false) }, 2000);
        }
    }
    const recaptchaV2SiteKey = process.env.NEXT_PUBLIC_EC_PRV_URL_SHORTENER_RECAPTCHA_V2_SITE_KEY;
    if (recaptchaV2SiteKey == null || recaptchaV2SiteKey.length == 0) {
        return (<>Missing reCaptcha V2 site key</>)
    };
    return (
        <>
            <form onSubmit={handleSubmit} className="flex flex-col">
                {shortenedUrlResult.length > 0 && (<div className="mt-10 mb-10 flex flex-row gap-3 px-2 justify-center text-lg mx-auto flex-wrap">
                    <a href={shortenedUrlResult}
                        className="text-indigo-500 hover:text-indigo-300 link link-primary"
                        target="_blank">{shortenedUrlResult}</a>
                    {shortenedUrlResult.length > 0 && (<button className="btn btn-secondary" onClick={copyToClipboard}>{didCopy ? "Copied!" : "Copy 📋"}</button>)}
                </div>)}
                <div className="flex justify-center">
                    <input type="text"
                        className="input input-bordered input-primary input-lg w-full sm:w-3/4"
                        value={urlToShorten}
                        onChange={handleChangeUrlToShorten}
                        required={true}
                        placeholder={`https://example.com/${new Date().getFullYear()}/${new Date().getMonth().toString().padStart(2, '0')}/${new Date().getDate().toString().padStart(2, '0')}/a-very-long-url.html`} />
                </div>
                <div className="py-2 flex mx-auto">
                    <ReCAPTCHA ref={e => { if (e != null) recaptchaRef.current = e; }}
                        sitekey={recaptchaV2SiteKey}
                        className="py-0"
                        onChange={handleRecaptchaV2Change} />
                </div>
                {isLoading ? (<span className="loading loading-spinner loading-lg mx-auto my-4"></span>) : (<button
                    disabled={!(recaptchaUserResponse.length > 0 && urlToShorten.length > 0)}
                    className="btn btn-primary btn-wide my-4 mx-auto"
                    type="submit">Shorten URL</button>)}
                {errored && (<div className="alert alert-error">
                    <svg xmlns="http://www.w3.org/2000/svg" className="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M10 14l2-2m0 0l2-2m-2 2l-2-2m2 2l2 2m7-2a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
                    <span>Something happened. Try again.</span>
                </div>)}
                {warned && (<div className="alert alert-warning">
                    <svg xmlns="http://www.w3.org/2000/svg" className="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" /></svg>
                    <span>{warned}</span>
                </div>
                )}
            </form>
        </>
    )
}
