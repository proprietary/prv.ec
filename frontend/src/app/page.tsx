'use client';

import { motion } from 'framer-motion';
import React from 'react';
import ShortenUrlBox from './ShortenUrlBox';

export default function Home() {
  const [yourTextHere, setYourTextHere] = React.useState('aa83ff');
  const exampleLongUrl = 'https://en.wikipedia.org/wiki/Main_Page';
  const exampleShortUrl = 'https://prv.ec/rF68Yns'
  return (
    <main className="antialiased text-gray-600 grid grid-flow-row">

      <div className="bg-slate-900 bg-gradient-to-tr from-slate-900 to-slate-950">
        <div className="max-w-screen-lg mx-auto px-3 pt-20 pb-32">
          <header className="text-center">
            <h1 className="whitespace-pre-line text-5xl font-bold dark:text-slate-200">
              prv.ec
            </h1>
            <div className="text-2xl mt-4 mb-16 text-center">Shorten your long URLs! ⚡️</div>
            <div>
              <motion.div className="text text-xs">
                {exampleLongUrl.split("").map((letter, idx) => (
                  <motion.span
                    className="font-mono"
                    key={idx}
                    initial={{ opacity: 1 }}
                    animate={{ opacity: 0 }}
                    transition={{ duration: 0.2, delay: 2 + idx * 0.1 }}>
                    {letter}
                  </motion.span>
                ))}
              </motion.div>
              <motion.span initial={{ opacity: 1 }} animate={{ opacity: 0 }} transition={{ delay: exampleLongUrl.length * 0.1 + 2, duration: 0.5 }}>⬇️</motion.span>
              <motion.div className="text text-lg">
                {exampleShortUrl.split("").map((letter, idx) => (
                  <motion.span className="font-mono"
                    key={idx}
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    transition={{ duration: 0.2, delay: 2 + idx * 0.1 }}>
                    {letter}
                  </motion.span>
                ))}
              </motion.div>
            </div>
          </header>
        </div>
      </div>

      <div className="py-5 mt-5 bg-black">
        <ShortenUrlBox />
      </div>

      <div className="px-2 py-20 md:px-10 bg-slate-950">
        <div className="divider"></div>

        <h3 className="text-lg mb-3 text-gray-300">A simple URL shortener with no frills</h3>
        <p className="py-2">
          No tracking or analytics features. (Get it? prv.ec sounds like "privacy.")
        </p>
        <p className="py-2">
          <code>prv.ec</code> is a simple utility for the web.
        </p>
        <p className="py-2">
          The server software behind this is open source and auditable.
          You are free to <a className="link text-indigo-800 hover:text-indigo-500" target="_blank" href="https://github.com/proprietary/ec_prv_url_shortener">fork this software</a> and white-label it on your own domain for your brand.
          It is written in C++ and designed to serve tens of thousands of requests per second with low hardware requirements.
        </p>
        <div className="divider"></div>
      </div>

      <footer className="footer p-10 bg-neutral text-neutral-content">
        <div className="text-sm">
          <p>&copy;2023 Zelly Snyder</p>
          <p>released under Apache-2.0 software license</p>
        </div>
        <div className="px-3">
          <h3 className="footer-title">Links</h3>
          <div className="grid grid-flow-col gap-4">
            <a target="_blank" href="https://github.com/proprietary/ec_prv_url_shortener"><svg width="24" height="24" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" className="fill-current"><path d="M256,32C132.3,32,32,134.9,32,261.7c0,101.5,64.2,187.5,153.2,217.9a17.56,17.56,0,0,0,3.8.4c8.3,0,11.5-6.1,11.5-11.4,0-5.5-.2-19.9-.3-39.1a102.4,102.4,0,0,1-22.6,2.7c-43.1,0-52.9-33.5-52.9-33.5-10.2-26.5-24.9-33.6-24.9-33.6-19.5-13.7-.1-14.1,1.4-14.1h.1c22.5,2,34.3,23.8,34.3,23.8,11.2,19.6,26.2,25.1,39.6,25.1a63,63,0,0,0,25.6-6c2-14.8,7.8-24.9,14.2-30.7-49.7-5.8-102-25.5-102-113.5,0-25.1,8.7-45.6,23-61.6-2.3-5.8-10-29.2,2.2-60.8a18.64,18.64,0,0,1,5-.5c8.1,0,26.4,3.1,56.6,24.1a208.21,208.21,0,0,1,112.2,0c30.2-21,48.5-24.1,56.6-24.1a18.64,18.64,0,0,1,5,.5c12.2,31.6,4.5,55,2.2,60.8,14.3,16.1,23,36.6,23,61.6,0,88.2-52.4,107.6-102.3,113.3,8,7.1,15.2,21.1,15.2,42.5,0,30.7-.3,55.5-.3,63,0,5.4,3.1,11.5,11.4,11.5a19.35,19.35,0,0,0,4-.4C415.9,449.2,480,363.1,480,261.7,480,134.9,379.7,32,256,32Z"></path></svg></a>
          </div>
        </div>
      </footer>
    </main>
  )
}
