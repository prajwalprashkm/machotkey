const taskbar = document.querySelector('.taskbar');
const dragRegion = document.querySelector('.drag-region');

window.hide_taskbar = () => {
    taskbar.style.display = 'none';
};
window.show_taskbar = () => {
    taskbar.style.display = 'flex';
    taskbar.style.opacity = '1';
};
window.soft_hide_taskbar = () => {
    taskbar.style.opacity = '0';
};
window.set_name = (name) => {
    dragRegion.textContent = name;
};

const startBtn = document.querySelectorAll('.icon-btn')[0];
const pauseBtn = document.querySelectorAll('.icon-btn')[1];
const stopBtn = document.querySelectorAll('.icon-btn')[2];

startBtn.disabled = true;
pauseBtn.disabled = true;
stopBtn.disabled = true;

startBtn.addEventListener('click', () => {
    if(startBtn.disabled) return;
    startBtn.disabled = true;
    stopBtn.disabled = false;
    pauseBtn.disabled = false;
    window.callCpp("overlay_start_macro", "");
});

pauseBtn.addEventListener('click', () => {
    if(pauseBtn.disabled) return;
    pauseBtn.disabled = true;
    stopBtn.disabled = false;
    startBtn.disabled = false;
    window.callCpp("overlay_pause_macro", "");
});

stopBtn.addEventListener('click', () => {
    if(stopBtn.disabled) return;
    stopBtn.disabled = true;
    pauseBtn.disabled = true;
    startBtn.disabled = false;
    window.callCpp("overlay_stop_macro", "");
});

window.set_btn_states = (a, b, c) => {
    startBtn.disabled = a;
    pauseBtn.disabled = b;
    stopBtn.disabled = c;
}

document.querySelectorAll('.icon-btn').forEach(btn => {
    btn.onmouseenter = () => btn.classList.add('is-hovered');
    btn.onmouseleave = () => btn.classList.remove('is-hovered');

    btn.addEventListener('click', () => {
        const animation = taskbar.getAnimations()[0];
        animation.updatePlaybackRate(2.0);
        setTimeout(()=>{animation.updatePlaybackRate(1.0);}, 1000);
        setTimeout(() => {
            btn.classList.remove('is-hovered');
            btn.blur(); // Removes any "focus" rings or states
            // 2. Force the hover state to break
            btn.classList.add('no-hover'); 
            btn.style.pointerEvents = 'none';
            
            // 3. Re-enable after a tiny delay
            setTimeout(() => {
                btn.style.pointerEvents = 'auto';
                btn.classList.remove('no-hover');
                btn.blur(); 
            }, 50);
        }, 100); // Small delay to let the click animation finish
    });
});
window.addEventListener("focusout", () => {
    document.querySelectorAll('.icon-btn').forEach(btn => {
        btn.classList.remove('is-hovered');
    });
});

window.addEventListener("load", () => {
    const id = setInterval(() => {
        if(!window.callCpp) return;
        const rect = taskbar.getBoundingClientRect();
        window.callCpp("overlay_dimensions", `${rect.width} ${rect.height}`);
        clearInterval(id);
    }, 1);
});

window.request_dimensions = () => {
    const rect = taskbar.getBoundingClientRect();
    window.callCpp("overlay_dimensions", `${rect.width} ${rect.height}`);
};