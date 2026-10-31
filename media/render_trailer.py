"""Render the 30s release trailer from actual gameplay + original synthesized audio.

Requires Python3.11+, Pillow13 and FFmpeg on PATH. Source capture rates are
preserved (10/6fps); the30fps export repeats frames without optical-flow invention.
python media/render_trailer.py --out artifacts/One-Punch-Man-Fallout4-Trailer.mp4
"""
import argparse
from array import array
from functools import lru_cache
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import wave

from PIL import Image, ImageDraw, ImageFont, ImageEnhance

W, H, FPS, DURATION = 1280, 720, 30, 30
YELLOW = (255, 216, 48)
WHITE = (246, 246, 239)


def make_score(path):
    """Original160BPM synth/percussion score. No samples, radio or commercial music."""
    rate = 44100
    rng = random.Random(20261031)
    pcm = array('h')
    beat_duration = 60 / 160
    roots = (73.4162, 73.4162, 58.2705, 65.4064)
    filtered = 0.0
    for i in range(rate * DURATION):
        t = i / rate
        beat = t / beat_duration
        integer_beat = int(beat)
        bt = (beat - integer_beat) * beat_duration
        eighth = (beat * 2 % 1) * beat_duration / 2
        root = roots[(integer_beat // 8) % len(roots)]
        story = 0.6 if 16 <= t < 21 else 1.0
        kick = math.sin(math.tau * (42 * bt + 9 * (1 - math.exp(-bt * 35)))) * math.exp(-bt * 18) * 0.5
        noise = rng.uniform(-1, 1)
        filtered += 0.12 * (noise - filtered)
        snare = (noise - filtered) * math.exp(-bt * 24) * (0.21 if integer_beat % 4 == 2 else 0.0)
        hat = (noise - filtered) * math.exp(-eighth * 110) * 0.055
        gate = (1 - math.exp(-eighth * 150)) * math.exp(-eighth * 15)
        bass = (math.sin(math.tau * root * t) + 0.22 * math.sin(math.tau * root * 2 * t)) * gate * 0.25
        pulse = math.sin(math.tau * root * (4 if integer_beat % 2 else 3) * t) * gate * 0.055
        pad = (math.sin(math.tau * root * 2 * t) + math.sin(math.tau * root * 2.3784 * t)) * 0.03
        bar_t = t % (beat_duration * 8)
        impact = (filtered * 0.34 + math.sin(math.tau * 38 * bar_t) * 0.32) * math.exp(-bar_t * 10)
        value = (kick + snare + hat + bass + pulse + pad + impact) * story
        value *= min(1.0, t / 0.035, (DURATION - t) / 0.6)
        pcm.append(round(math.tanh(value * 1.15) * 27000))
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--timeline', type=Path, default=Path(__file__).with_name('timeline.json'))
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--font', type=Path)
    args = parser.parse_args()
    ffmpeg = shutil.which(args.ffmpeg) or args.ffmpeg
    spec = json.loads(args.timeline.read_text(encoding='utf-8'))
    base = args.timeline.parent
    sources = {x['id']: x for x in spec['sources']}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    font_root = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
    headline = args.font or font_root / 'impact.ttf'
    body = font_root / 'segoeui.ttf'
    if not body.exists():
        body = headline

    @lru_cache(maxsize=32)
    def font(size, light=False):
        return ImageFont.truetype(str(body if light else headline), size)

    def text(draw, position, value, size, color=WHITE, light=False):
        draw.text(position, value, font=font(size, light), fill=color,
                  stroke_width=1 if light else 2, stroke_fill=(5, 9, 14))

    @lru_cache(maxsize=12)
    def still(source, number):
        return Image.open(base / sources[source]['directory'] / f'f_{number:05d}.jpg').convert('RGB').resize((W, H), Image.Resampling.LANCZOS)

    hero = sources['hero']
    hero_path = base / hero['path']
    raw = subprocess.check_output([ffmpeg, '-v', 'error', '-ss', '2', '-i', str(hero_path), '-t', '4',
                                   '-vf', f'scale={W}:{H}:flags=lanczos,fps=6', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    frame_bytes = W * H * 3
    hero_frames = [Image.frombytes('RGB', (W, H), raw[n:n+frame_bytes]) for n in range(0, len(raw), frame_bytes)]
    assert len(hero_frames) == 24

    gradient = Image.new('RGBA', (W, H))
    gd = ImageDraw.Draw(gradient)
    for y in range(H):
        alpha = int(max(0, 0.8 - y / 350) * 190 + max(0, (y - 570) / 150) * 125)
        gd.line((0, y, W, y), fill=(3, 8, 14, min(220, alpha)))

    def frame(t):
        if t < 3:
            image = still('finB', 100 + min(29, int(t * 10))).copy()
        elif t < 6:
            image = still('look', 750 + min(29, int((t - 3) * 10))).copy()
        elif t < 9:
            image = still('finA2', 23 + min(29, int((t - 6) * 10))).copy()
        elif t < 12:
            image = still('finA2', 114 + min(29, int((t - 9) * 10))).copy()
        elif t < 16:
            image = hero_frames[min(23, int((t - 12) * 6))].copy()
        elif t < 21:
            image = still('look', 765).copy()
        elif t < 25:
            image = still('finB', 103 + min(39, int((t - 21) * 10))).copy()
        else:
            image = still('look', 765).copy()
        image = ImageEnhance.Contrast(image).enhance(1.08).convert('RGBA')
        image = Image.alpha_composite(image, gradient)
        if 16 <= t < 21:
            image = Image.alpha_composite(image, Image.new('RGBA', (W, H), (3, 8, 14, 165)))
        if t >= 25:
            image = Image.alpha_composite(image, Image.new('RGBA', (W, H), (3, 8, 14, 190)))
        draw = ImageDraw.Draw(image)
        draw.rectangle((48, 45, 55, 84), fill=YELLOW)
        text(draw, (71, 47), '0MIS  /  FALLOUT 4 MOD', 22, WHITE, True)
        if t < 3:
            text(draw, (46, 102), 'ONE PUNCH.', 100)
            text(draw, (48, 213), 'A WHOLE NEW PROBLEM.', 43, YELLOW)
        elif t < 6:
            text(draw, (48, 109), 'MEET YOUR', 76)
            text(draw, (48, 196), 'NEW COMPANION.', 76, YELLOW)
        elif t < 9:
            text(draw, (48, 111), 'FLYING PUNCHES.', 82)
        elif t < 12:
            text(draw, (48, 109), 'THREAT.', 78)
            text(draw, (48, 196), 'REMOVED.', 78, YELLOW)
        elif t < 16:
            text(draw, (48, 111), 'OVER A MONTH', 80)
            text(draw, (48, 202), 'IN THE MAKING.', 80, YELLOW)
            text(draw, (52, 315), 'A project by 0mis', 27, WHITE, True)
        elif t < 21:
            text(draw, (48, 128), '~35 BILLION', 92)
            text(draw, (48, 229), 'TOKENS*', 92, YELLOW)
            text(draw, (48, 373), '~$400,000', 75)
            text(draw, (52, 463), 'API-EQUIVALENT COMPUTE*', 30, YELLOW, True)
            text(draw, (52, 592), '*Creator estimates. Not verified spending.', 27, WHITE, True)
        elif t < 25:
            text(draw, (48, 111), 'FREE MOD.', 86)
            text(draw, (48, 206), 'OPEN-SOURCE CODE.', 64, YELLOW)
        else:
            text(draw, (48, 126), 'ONE PUNCH MAN', 105)
            text(draw, (52, 252), 'FALLOUT 4 COMPANION', 57, YELLOW)
            text(draw, (53, 330), 'SUPER PEASANT  /  PUBLIC PREVIEW', 28, WHITE, True)
            draw.rounded_rectangle((50, 415, 497, 480), radius=8, fill=YELLOW)
            draw.text((76, 424), 'DOWNLOAD + SOURCE', font=font(36), fill=(8, 15, 21))
            text(draw, (52, 511), 'github.com/0mis/fallout4-super-peasant', 35, WHITE, True)
            text(draw, (52, 619), 'Unofficial fan mod. Requirements and installation on GitHub.', 23, WHITE, True)
        if t < 16 or 21 <= t < 25:
            draw.rectangle((0, 678, W, H), fill=(3, 8, 14, 225))
            text(draw, (48, 685), 'ACTUAL FALLOUT 4 GAMEPLAY  |  DEVELOPMENT FOOTAGE', 18, (200, 205, 208), True)
        if t > 29.65:
            image = Image.alpha_composite(image, Image.new('RGBA', (W, H), (0, 0, 0, min(255, int((t - 29.65) / 0.35 * 255)))))
        return image.convert('RGB')

    silent = args.out.with_name(args.out.stem + '-silent.mp4')
    score = args.out.with_name(args.out.stem + '-original-score.wav')
    make_score(score)
    command = [ffmpeg, '-v', 'warning', '-y', '-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', f'{W}x{H}',
               '-framerate', str(FPS), '-i', '-', '-an', '-c:v', 'libx264', '-preset', 'medium', '-crf', '19',
               '-pix_fmt', 'yuv420p', '-profile:v', 'high', '-level:v', '3.1', '-movflags', '+faststart', str(silent)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE)
    try:
        for n in range(FPS * DURATION):
            process.stdin.write(frame(n / FPS).tobytes())
        process.stdin.close()
        if process.wait() != 0:
            raise RuntimeError('Video encoding failed')
    except BaseException:
        process.kill()
        process.wait()
        raise
    subprocess.run([ffmpeg, '-v', 'warning', '-y', '-i', str(silent), '-i', str(score), '-map', '0:v:0', '-map', '1:a:0',
                    '-c:v', 'copy', '-c:a', 'aac', '-b:a', '160k', '-ac', '2', '-ar', '48000',
                    '-af', 'loudnorm=I=-16:LRA=9:TP=-1.5', '-t', str(DURATION), '-movflags', '+faststart',
                    '-metadata', 'title=One Punch Man x Fallout4 - Super Peasant release',
                    '-metadata', 'comment=Development gameplay. Creator estimates are not verified spending. Original synthesized score.',
                    str(args.out)], check=True)
    sheet = Image.new('RGB', (W, 3 * 218), (8, 15, 21))
    sd = ImageDraw.Draw(sheet)
    for n, t in enumerate((0.4, 3.4, 6.5, 10.0, 13.6, 16.5, 18.7, 20.5, 22.5, 25.6, 27.0, 29.1)):
        x, y = (n % 4) * 320, (n // 4) * 218
        sheet.paste(frame(t).resize((320, 180), Image.Resampling.LANCZOS), (x, y))
        sd.text((x + 8, y + 184), f'{t:.1f}s', font=font(20, True), fill=WHITE)
    sheet.save(args.out.with_name(args.out.stem + '-review.jpg'), quality=95)
    frame(27).save(args.out.with_name(args.out.stem + '-poster.jpg'), quality=95)
    receipt = {'duration': DURATION, 'width': W, 'height': H, 'fps': FPS,
               'gameplay_source_fps': [10, 6], 'optical_flow': False, 'audio': 'Original procedural score',
               'creator_figures_are_estimates': True, 'development_gameplay_label': True,
               'sha256': hashlib.sha256(args.out.read_bytes()).hexdigest(), 'bytes': args.out.stat().st_size}
    args.out.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
