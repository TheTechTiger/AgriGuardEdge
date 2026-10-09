/**
 * AgriGuardEdge — Precision Scrollytelling & Telemetry Engine
 * - Sequence 1: Fully pinned scrollytelling track (plays frames 1 to 248 completely with chapter overlays)
 * - Sequence 2: Continuous auto-loop 3D hardware rotation
 * - Microinteractions: Custom cursor glow, 3D card tilt, magnetic buttons, animated stat counters, and live waveform simulator
 */

(() => {
    // Frame counts
    const TOTAL_SEQ1_FRAMES = 248;
    const TOTAL_SEQ2_FRAMES = 250;

    // Image Caches
    const seq1Images = new Array(TOTAL_SEQ1_FRAMES);
    const seq2Images = new Array(TOTAL_SEQ2_FRAMES);

    // Canvas Elements
    const heroCanvas = document.getElementById('hero-canvas');
    const heroCtx = heroCanvas ? heroCanvas.getContext('2d') : null;
    const seq2Canvas = document.getElementById('sequence2-canvas');
    const seq2Ctx = seq2Canvas ? seq2Canvas.getContext('2d') : null;

    // Sequence 1 Scroll Playback State
    let currentHeroFrame = 0;
    let targetHeroFrame = 0;

    // Sequence 2 Auto-Loop State
    let seq2CurrentFrame = 0;
    const seq2PlaybackSpeed = 0.65; // Cinematic 3D rotation speed

    // UI Progress Indicators
    const seqFill = document.getElementById('seq-fill');
    const seqFrameDisplay = document.getElementById('seq-frame-display');

    // Preloader Elements
    const preloader = document.getElementById('preloader');
    const preloaderBar = document.getElementById('preloader-bar');
    const preloaderPercent = document.getElementById('preloader-percent');

    const padNumber = (num, size = 3) => num.toString().padStart(size, '0');

    // ----------------------------------------------------
    // 1. CANVAS SIZING & COVER MATH
    // ----------------------------------------------------
    const resizeCanvases = () => {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);

        if (heroCanvas) {
            heroCanvas.width = window.innerWidth * dpr;
            heroCanvas.height = window.innerHeight * dpr;
        }

        if (seq2Canvas && seq2Canvas.parentElement) {
            seq2Canvas.width = seq2Canvas.parentElement.clientWidth * dpr;
            seq2Canvas.height = seq2Canvas.parentElement.clientHeight * dpr;
        }

        drawHeroFrame(currentHeroFrame);
        drawSeq2Frame(seq2CurrentFrame);
    };

    window.addEventListener('resize', resizeCanvases);

    const drawCover = (ctx, img, canvas) => {
        if (!ctx || !img || !canvas) return;

        const cw = canvas.width;
        const ch = canvas.height;
        const imgRatio = img.width / img.height;
        const canvasRatio = cw / ch;

        let drawWidth, drawHeight, offsetX, offsetY;

        if (imgRatio > canvasRatio) {
            drawHeight = ch;
            drawWidth = ch * imgRatio;
            offsetX = (cw - drawWidth) / 2;
            offsetY = 0;
        } else {
            drawWidth = cw;
            drawHeight = cw / imgRatio;
            offsetX = 0;
            offsetY = (ch - drawHeight) / 2;
        }

        ctx.clearRect(0, 0, cw, ch);
        ctx.drawImage(img, offsetX, offsetY, drawWidth, drawHeight);
    };

    // Fast O(1) nearest loaded frame fallback
    const getNearestLoadedFrame = (arr, targetIndex) => {
        if (arr[targetIndex]) return arr[targetIndex];
        const len = arr.length;
        for (let offset = 1; offset < len; offset++) {
            if (targetIndex - offset >= 0 && arr[targetIndex - offset]) {
                return arr[targetIndex - offset];
            }
            if (targetIndex + offset < len && arr[targetIndex + offset]) {
                return arr[targetIndex + offset];
            }
        }
        return null;
    };

    const drawHeroFrame = (frameIndex) => {
        const idx = Math.min(TOTAL_SEQ1_FRAMES - 1, Math.max(0, Math.round(frameIndex)));
        const img = getNearestLoadedFrame(seq1Images, idx);
        if (img && heroCtx && heroCanvas) {
            drawCover(heroCtx, img, heroCanvas);
        }
    };

    const drawSeq2Frame = (frameIndex) => {
        const idx = Math.min(TOTAL_SEQ2_FRAMES - 1, Math.max(0, Math.round(frameIndex)));
        const img = getNearestLoadedFrame(seq2Images, idx);
        if (img && seq2Ctx && seq2Canvas) {
            drawCover(seq2Ctx, img, seq2Canvas);
        }
    };

    // ----------------------------------------------------
    // 2. MAIN 60FPS RENDERING LOOP
    // ----------------------------------------------------
    const renderLoop = () => {
        // 1. Sequence 1 (Hero Background Scroll Scrubbing)
        const heroDiff = targetHeroFrame - currentHeroFrame;
        if (Math.abs(heroDiff) > 0.002) {
            currentHeroFrame += heroDiff * 0.22;
            drawHeroFrame(currentHeroFrame);
            if (seqFrameDisplay) {
                const currentNum = Math.min(TOTAL_SEQ1_FRAMES, Math.max(1, Math.round(currentHeroFrame + 1)));
                seqFrameDisplay.textContent = `${padNumber(currentNum)} / ${TOTAL_SEQ1_FRAMES}`;
            }
        }

        // 2. Sequence 2 (Hardware Continuous 3D Loop)
        seq2CurrentFrame = (seq2CurrentFrame + seq2PlaybackSpeed) % TOTAL_SEQ2_FRAMES;
        drawSeq2Frame(seq2CurrentFrame);

        requestAnimationFrame(renderLoop);
    };

    // ----------------------------------------------------
    // 3. PROGRESSIVE ASYNC IMAGE STREAMER
    // ----------------------------------------------------
    const loadImageAsync = (src, onLoad) => {
        const img = new Image();
        img.src = src;
        img.onload = () => onLoad(img);
        img.onerror = () => onLoad(null);
    };

    const dismissPreloader = () => {
        if (preloader && !preloader.classList.contains('fade-out')) {
            preloader.classList.add('fade-out');
            document.body.classList.remove('loading');
        }
    };

    const loadSequencesProgressively = () => {
        const total = TOTAL_SEQ1_FRAMES + TOTAL_SEQ2_FRAMES;
        let loaded = 0;

        // Load Frame 001 immediately
        loadImageAsync(`assets/image_sequence1/ezgif-frame-001.jpg`, (img) => {
            if (img) {
                seq1Images[0] = img;
                drawHeroFrame(0);
            }
            setTimeout(dismissPreloader, 300);
        });

        loadImageAsync(`assets/image_sequence2/ezgif-frame-001.jpg`, (img) => {
            if (img) {
                seq2Images[0] = img;
                drawSeq2Frame(0);
            }
        });

        const updateProgress = () => {
            loaded++;
            const pct = Math.min(100, Math.round((loaded / total) * 100));
            if (preloaderBar) preloaderBar.style.width = `${pct}%`;
            if (preloaderPercent) preloaderPercent.textContent = `${pct}%`;
            if (pct >= 90) dismissPreloader();
        };

        // Stream Sequence 1 in order
        for (let i = 1; i <= TOTAL_SEQ1_FRAMES; i++) {
            const index = i - 1;
            const url = `assets/image_sequence1/ezgif-frame-${padNumber(i)}.jpg`;
            loadImageAsync(url, (img) => {
                if (img) seq1Images[index] = img;
                updateProgress();
            });
        }

        // Stream Sequence 2 in order
        for (let i = 1; i <= TOTAL_SEQ2_FRAMES; i++) {
            const index = i - 1;
            const url = `assets/image_sequence2/ezgif-frame-${padNumber(i)}.jpg`;
            loadImageAsync(url, (img) => {
                if (img) seq2Images[index] = img;
                updateProgress();
            });
        }

        setTimeout(dismissPreloader, 1000);
    };

    // ----------------------------------------------------
    // 4. PINNED SCROLLTRIGGER FOR SEQUENCE 1 & CHAPTERS
    // ----------------------------------------------------
    const initPinnedScrollytelling = () => {
        if (typeof gsap === 'undefined' || typeof ScrollTrigger === 'undefined') return;

        gsap.registerPlugin(ScrollTrigger);

        // Header Background on Scroll
        const nav = document.getElementById('main-nav');
        ScrollTrigger.create({
            start: 'top -40',
            end: 99999,
            toggleClass: { className: 'scrolled', targets: nav }
        });

        const chapters = [
            document.getElementById('chapter-1'),
            document.getElementById('chapter-2'),
            document.getElementById('chapter-3'),
            document.getElementById('chapter-4')
        ];

        const setChapterActive = (activeIndex) => {
            chapters.forEach((ch, idx) => {
                if (!ch) return;
                if (idx === activeIndex) {
                    ch.classList.add('active');
                } else {
                    ch.classList.remove('active');
                }
            });
        };

        // PINNED MASTER TIMELINE: Plays Sequence 1 completely (frames 1..248) across 400vh virtual scroll
        ScrollTrigger.create({
            trigger: '#hero-scrolly-track',
            start: 'top top',
            end: '+=450%', // Gives 450vh of smooth scrollytelling before unlocking downstream sections
            pin: true,
            scrub: 0.25,
            onUpdate: (self) => {
                const progress = self.progress; // 0.0 to 1.0
                targetHeroFrame = progress * (TOTAL_SEQ1_FRAMES - 1);

                // Update Sequence 1 Progress Pill
                if (seqFill) {
                    seqFill.style.width = `${progress * 100}%`;
                }

                // Switch narrative chapters based on narrative milestones
                if (progress < 0.25) {
                    setChapterActive(0); // Chapter 1: Hero
                } else if (progress >= 0.25 && progress < 0.52) {
                    setChapterActive(1); // Chapter 2: Macro Subsurface
                } else if (progress >= 0.52 && progress < 0.78) {
                    setChapterActive(2); // Chapter 3: Rain & Sprout Fusion
                } else {
                    setChapterActive(3); // Chapter 4: Forest Canopy Completion
                }
            }
        });

        // ----------------------------------------------------
        // DOWNSTREAM SECTIONS SCROLL TRIGGERS (REVEAL AFTER PIN)
        // ----------------------------------------------------

        // 1. Statement Section Reveal
        gsap.fromTo('.gsap-statement-badges',
            { opacity: 0, y: 25 },
            { opacity: 1, y: 0, duration: 0.7, scrollTrigger: { trigger: '#problem', start: 'top 80%' } }
        );
        gsap.fromTo('.gsap-statement-title',
            { opacity: 0, y: 35 },
            { opacity: 1, y: 0, duration: 0.9, scrollTrigger: { trigger: '#problem', start: 'top 75%' } }
        );
        gsap.fromTo('.gsap-statement-body',
            { opacity: 0, y: 25 },
            { opacity: 1, y: 0, duration: 0.8, scrollTrigger: { trigger: '#problem', start: 'top 70%' } }
        );
        gsap.fromTo('.floating-glass-card',
            { opacity: 0, y: 40 },
            { opacity: 1, y: 0, stagger: 0.15, duration: 0.8, scrollTrigger: { trigger: '.parallax-cards-container', start: 'top 80%' } }
        );

        // 2. Cream Bento Grid
        gsap.fromTo('.gsap-cream-head',
            { opacity: 0, y: 40 },
            { opacity: 1, y: 0, duration: 0.8, scrollTrigger: { trigger: '#how-it-works', start: 'top 75%' } }
        );
        gsap.fromTo('.bento-card',
            { opacity: 0, y: 40 },
            { opacity: 1, y: 0, stagger: 0.12, duration: 0.8, scrollTrigger: { trigger: '.bento-grid', start: 'top 80%' } }
        );

        // 3. Hardware Showcase Reveal
        gsap.fromTo('.gsap-hw-visual',
            { opacity: 0, x: -30 },
            { opacity: 1, x: 0, duration: 0.8, scrollTrigger: { trigger: '.hardware-interactive-showcase', start: 'top 80%' } }
        );
        gsap.fromTo('.hw-card',
            { opacity: 0, x: 30 },
            { opacity: 1, x: 0, stagger: 0.1, duration: 0.7, scrollTrigger: { trigger: '.hw-modules-list', start: 'top 80%' } }
        );

        // 4. Insights Metrics & Counter Trigger
        gsap.fromTo('.metric-card',
            { opacity: 0, y: 40 },
            {
                opacity: 1,
                y: 0,
                stagger: 0.15,
                duration: 0.8,
                scrollTrigger: {
                    trigger: '#system-insights',
                    start: 'top 75%',
                    onEnter: triggerCounterAnimations
                }
            }
        );

        // 5. Mega Footer
        gsap.fromTo('.gsap-footer-cta',
            { opacity: 0, scale: 0.95 },
            { opacity: 1, scale: 1, duration: 0.8, scrollTrigger: { trigger: '#final-cta', start: 'top 75%' } }
        );
        gsap.fromTo('.giant-wordmark',
            { opacity: 0, y: 60 },
            { opacity: 0.95, y: 0, duration: 1, ease: 'power2.out', scrollTrigger: { trigger: '.giant-wordmark-wrap', start: 'top 90%' } }
        );
    };

    // ----------------------------------------------------
    // 5. MICROINTERACTIONS: 3D CARD TILT & MAGNETIC BUTTONS
    // ----------------------------------------------------
    const setupMicrointeractions = () => {
        // 1. Custom Glow Cursor Follower
        const cursorGlow = document.getElementById('cursor-glow');
        if (cursorGlow) {
            let mouseX = window.innerWidth / 2;
            let mouseY = window.innerHeight / 2;
            let currentX = mouseX;
            let currentY = mouseY;

            window.addEventListener('mousemove', (e) => {
                mouseX = e.clientX;
                mouseY = e.clientY;
            }, { passive: true });

            const updateCursor = () => {
                currentX += (mouseX - currentX) * 0.12;
                currentY += (mouseY - currentY) * 0.12;
                cursorGlow.style.left = `${currentX}px`;
                cursorGlow.style.top = `${currentY}px`;
                requestAnimationFrame(updateCursor);
            };
            updateCursor();
        }

        // 2. 3D Tilt Cards (.tilt-card)
        const tiltCards = document.querySelectorAll('.tilt-card');
        tiltCards.forEach((card) => {
            card.addEventListener('mousemove', (e) => {
                const rect = card.getBoundingClientRect();
                const x = e.clientX - rect.left;
                const y = e.clientY - rect.top;
                const centerX = rect.width / 2;
                const centerY = rect.height / 2;
                const rotateX = ((y - centerY) / centerY) * -7;
                const rotateY = ((x - centerX) / centerX) * 7;

                card.style.transform = `perspective(1000px) rotateX(${rotateX.toFixed(2)}deg) rotateY(${rotateY.toFixed(2)}deg) translateY(-6px)`;
            });

            card.addEventListener('mouseleave', () => {
                card.style.transform = 'perspective(1000px) rotateX(0deg) rotateY(0deg) translateY(0px)';
            });
        });

        // 3. Magnetic Buttons (.magnetic-btn)
        const magneticBtns = document.querySelectorAll('.magnetic-btn');
        magneticBtns.forEach((btn) => {
            btn.addEventListener('mousemove', (e) => {
                const rect = btn.getBoundingClientRect();
                const x = e.clientX - rect.left - rect.width / 2;
                const y = e.clientY - rect.top - rect.height / 2;
                btn.style.transform = `translate(${x * 0.25}px, ${y * 0.25}px)`;
            });

            btn.addEventListener('mouseleave', () => {
                btn.style.transform = 'translate(0px, 0px)';
            });
        });
    };

    // ----------------------------------------------------
    // 6. ANIMATED NUMBER COUNTERS
    // ----------------------------------------------------
    let countersAnimated = false;
    const triggerCounterAnimations = () => {
        if (countersAnimated) return;
        countersAnimated = true;

        const counterElements = document.querySelectorAll('.counter-val');
        counterElements.forEach((el) => {
            const target = parseFloat(el.getAttribute('data-target')) || 0;
            const suffix = el.getAttribute('data-suffix') || '';
            const isDecimal = target % 1 !== 0;
            const duration = 1500;
            const startTime = performance.now();

            const updateCount = (now) => {
                const elapsed = now - startTime;
                const progress = Math.min(1, elapsed / duration);
                // Ease out quad
                const ease = 1 - (1 - progress) * (1 - progress);
                const current = target * ease;

                el.textContent = `${isDecimal ? current.toFixed(2) : Math.round(current)}${suffix}`;

                if (progress < 1) {
                    requestAnimationFrame(updateCount);
                } else {
                    el.textContent = `${target}${suffix}`;
                }
            };
            requestAnimationFrame(updateCount);
        });
    };

    // ----------------------------------------------------
    // 7. HARDWARE CARDS INTERACTION (CARD JUMP & AUTO ROTATION)
    // ----------------------------------------------------
    const setupHardwareInteractions = () => {
        const cards = document.querySelectorAll('.hw-card');
        cards.forEach((card) => {
            const targetFrame = parseInt(card.getAttribute('data-frame-target'), 10) || 0;
            const activate = () => {
                cards.forEach(c => c.classList.remove('active'));
                card.classList.add('active');
                seq2CurrentFrame = targetFrame;
            };
            card.addEventListener('mouseenter', activate);
            card.addEventListener('click', activate);
        });
    };

    // ----------------------------------------------------
    // 8. LIVE ANOMALY RADAR / WAVEFORM SIMULATOR
    // ----------------------------------------------------
    const setupWaveformSimulator = () => {
        const canvas = document.getElementById('waveform-canvas');
        if (!canvas) return;
        const ctx = canvas.getContext('2d');

        const btnLeak = document.getElementById('btn-inject-leak');
        const btnFault = document.getElementById('btn-inject-fault');
        const btnReset = document.getElementById('btn-reset-sim');

        const valVoltage = document.getElementById('val-voltage');
        const valMoisture = document.getElementById('val-moisture');
        const valScore = document.getElementById('val-score');
        const valRelay = document.getElementById('val-relay');

        let mode = 'normal';
        let time = 0;
        const historyData = new Array(120).fill(41.0);

        const resizeWaveform = () => {
            const dpr = Math.min(window.devicePixelRatio || 1, 2);
            if (canvas.parentElement) {
                canvas.width = canvas.parentElement.clientWidth * dpr;
                canvas.height = canvas.parentElement.clientHeight * dpr;
            }
        };
        window.addEventListener('resize', resizeWaveform);
        resizeWaveform();

        if (btnLeak) btnLeak.addEventListener('click', () => { mode = 'leak'; });
        if (btnFault) btnFault.addEventListener('click', () => { mode = 'fault'; });
        if (btnReset) btnReset.addEventListener('click', () => { mode = 'normal'; });

        const renderWaveform = () => {
            time += 0.06;
            const w = canvas.width;
            const h = canvas.height;
            ctx.clearRect(0, 0, w, h);

            let currentVal = 41.2 + Math.sin(time * 1.5) * 1.5 + (Math.random() - 0.5) * 0.8;
            let currentVoltage = 3.24 + (Math.random() - 0.5) * 0.04;
            let currentScore = 0.04 + Math.random() * 0.03;

            if (mode === 'leak') {
                currentVal = 78.5 + Math.sin(time * 3) * 5.0 + (Math.random() - 0.5) * 2.0;
                currentVoltage = 2.15 + (Math.random() - 0.5) * 0.1;
                currentScore = 0.94 + Math.random() * 0.05;
                if (valRelay) {
                    valRelay.textContent = 'TRIPPED: VALVE CLOSED';
                    valRelay.className = 'stat-val status-red';
                }
            } else if (mode === 'fault') {
                currentVal = 14.0 + (Math.random() - 0.5) * 8.0;
                currentVoltage = 0.85 + (Math.random() - 0.5) * 0.3;
                currentScore = 0.88 + Math.random() * 0.08;
                if (valRelay) {
                    valRelay.textContent = 'FAULT: SENSOR DRIFT';
                    valRelay.className = 'stat-val status-red';
                }
            } else {
                if (valRelay) {
                    valRelay.textContent = 'OPEN (NORMAL IRRIGATION)';
                    valRelay.className = 'stat-val status-green';
                }
            }

            if (valVoltage) valVoltage.textContent = `${currentVoltage.toFixed(2)} V`;
            if (valMoisture) valMoisture.textContent = `${currentVal.toFixed(1)} %`;
            if (valScore) valScore.textContent = currentScore.toFixed(2);

            historyData.push(currentVal);
            historyData.shift();

            // Background Grid Lines
            ctx.strokeStyle = 'rgba(255, 255, 255, 0.06)';
            ctx.lineWidth = 1;
            const stepY = h / 4;
            for (let y = stepY; y < h; y += stepY) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(w, y);
                ctx.stroke();
            }

            // Anomaly Threshold Line
            const thresholdY = h * 0.35;
            ctx.strokeStyle = 'rgba(239, 68, 68, 0.4)';
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.moveTo(0, thresholdY);
            ctx.lineTo(w, thresholdY);
            ctx.stroke();
            ctx.setLineDash([]);

            // Waveform Plot
            ctx.beginPath();
            const stepX = w / (historyData.length - 1);
            for (let i = 0; i < historyData.length; i++) {
                const val = historyData[i];
                const y = h - (val / 100) * (h * 0.85) - h * 0.08;
                const x = i * stepX;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }

            ctx.strokeStyle = (mode === 'leak' || mode === 'fault') ? '#ef4444' : '#b6f36a';
            ctx.lineWidth = 2.5;
            ctx.stroke();

            requestAnimationFrame(renderWaveform);
        };

        renderWaveform();
    };

    // ----------------------------------------------------
    // 9. BACK TO TOP
    // ----------------------------------------------------
    const setupBackToTop = () => {
        const btn = document.getElementById('btn-back-to-top');
        if (btn) {
            btn.addEventListener('click', () => {
                window.scrollTo({ top: 0, behavior: 'smooth' });
            });
        }
    };

    // ----------------------------------------------------
    // 10. INITIALIZE EVERYTHING
    // ----------------------------------------------------
    const init = () => {
        resizeCanvases();
        renderLoop();
        initPinnedScrollytelling();
        loadSequencesProgressively();
        setupMicrointeractions();
        setupHardwareInteractions();
        setupWaveformSimulator();
        setupBackToTop();
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
