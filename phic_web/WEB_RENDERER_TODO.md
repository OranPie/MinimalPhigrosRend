# Web Renderer Implementation TODO

**Target**: Interactive Phigros chart player in browser
**Tech Stack**: PixiJS v8 + React + TypeScript + AudioContext

---

## 🎯 Phase 2: Web Renderer (CURRENT FOCUS)

### Priority 1: Core Runtime Logic (CRITICAL)

#### 2.1 Kinematics System ⏳
**Source**: `phic_renderer/runtime/kinematics.py`
**Target**: `frontend/src/runtime/kinematics.ts`

**Tasks**:
- [ ] Port `eval_line_state()` function
  - Evaluate line position, rotation, alpha at time t
  - Handle state overrides (force_line_alpha01)
  - Return 6-tuple: (x, y, rot, alpha01, scroll, alpha_raw)

- [ ] Port `note_world_pos()` function (CRITICAL)
  - Transform note from local to world coordinates
  - Handle tangent/normal vectors
  - Apply scroll delta with speed multiplier
  - Handle `hold_keep_head` for hold notes
  - **This is THE most critical function for accurate rendering**

**Code Reference**: Lines 10-67 in `kinematics.py`

**Validation**:
- [ ] Unit tests comparing with Python output
- [ ] Visual test: notes should appear at exact same positions

---

#### 2.2 Judge System ⏳
**Source**: `phic_renderer/runtime/judge.py`
**Target**: `frontend/src/runtime/Judge.ts`

**Tasks**:
- [ ] Create Judge class with timing windows
  - PERFECT: ±45ms
  - GOOD: ±90ms
  - BAD: ±150ms

- [ ] Implement `tryHit()` method
  - Evaluate hit timing
  - Update combo and accuracy
  - Return grade ('PERFECT' | 'GOOD' | 'BAD')

- [ ] Implement `markMiss()` method
  - Break combo
  - Update accuracy

- [ ] Add timing offset tracking (for calibration)
  - Track early/late hits
  - Calculate median offset

**Enhancements** (vs Python):
- [ ] Sub-millisecond precision using AudioContext.currentTime
- [ ] Accuracy gradient within PERFECT window (1.0 → 0.9)
- [ ] Visual timing feedback (early/late indicator)

**Code Reference**: Lines 3-70 in `judge.py`

---

#### 2.3 Input Handler ⏳
**Source**: `phic_renderer/renderer/pygame/manual_judgement.py`
**Target**: `frontend/src/runtime/InputHandler.ts`

**Tasks**:
- [ ] Multi-touch tracking
  ```typescript
  class InputHandler {
    private pointers: Map<number, PointerState>
    onPointerDown(e: PointerEvent)
    onPointerMove(e: PointerEvent)
    onPointerUp(e: PointerEvent)
  }
  ```

- [ ] Gesture recognition
  - Tap: Quick press + release
  - Hold: Press + hold for duration
  - Drag: Move while pressed
  - Flick: Fast swipe motion (velocity threshold)

- [ ] Spatial hit detection
  - Port `_pick_best_candidate()` function
  - Judge width calculation (12% of screen width)
  - Find closest note in time + space

**Code Reference**: Lines 25-64 in `manual_judgement.py`

---

### Priority 2: PixiJS Renderer (CRITICAL)

#### 2.4 Renderer Architecture ⏳
**Source**: `phic_renderer/renderer/pygame/frame_renderer.py`
**Target**: `frontend/src/renderer/`

**Tasks**:
- [ ] Create main PixiJS Application
  ```typescript
  class PixiRenderer {
    private app: Application
    private layers: {
      background: BackgroundLayer
      lines: JudgmentLineLayer
      notes: NoteLayer
      hitfx: HitFXLayer
      ui: UILayer
    }
  }
  ```

- [ ] Implement layered rendering
  - Each layer is a PIXI.Container
  - Render order: background → lines → notes → hitfx → ui
  - Independent update/render cycles

**Architecture Pattern**:
```
PixiRenderer (main app)
  └─ Application (PixiJS)
      ├─ BackgroundLayer (Container)
      ├─ JudgmentLineLayer (Container)
      ├─ NoteLayer (Container)
      │   └─ NoteSprite (Sprite pool)
      ├─ HitFXLayer (Container)
      └─ UILayer (Container)
```

---

#### 2.5 Background Layer ⏳
**Tasks**:
- [ ] Load and display background image
- [ ] Apply blur effect (PixiJS BlurFilter)
- [ ] Dim overlay (dark rectangle with alpha)
- [ ] Resize to fit canvas

---

#### 2.6 Judgment Line Layer ⏳
**Tasks**:
- [ ] Render judgment lines as Graphics
  ```typescript
  class JudgmentLineLayer {
    renderLine(line: RuntimeLine, time: number) {
      const [x, y, rot, alpha, scroll] = evalLineState(line, time)
      // Draw line with rotation and alpha
    }
  }
  ```

- [ ] Line visual properties
  - Color (from line.color_rgb)
  - Width (configurable, default ~4px)
  - Alpha transparency
  - Rotation transform

- [ ] Text overlay (if line.text exists)
  - PIXI.Text with dynamic content
  - Positioned on line

- [ ] Texture overlay (if line.texture_path exists)
  - PIXI.Sprite
  - Anchored and rotated with line

---

#### 2.7 Note Layer ⏳ (MOST COMPLEX)
**Tasks**:
- [ ] Sprite pooling system
  ```typescript
  class SpritePool {
    acquire(textureKey: string): Sprite
    release(textureKey: string, sprite: Sprite)
  }
  ```

- [ ] Note sprite rendering
  - Calculate world position using `noteWorldPos()`
  - Set sprite position, rotation, scale, alpha
  - Apply tint color for note type

- [ ] Hold note rendering
  - 3-slice approach: head, body, tail
  - Stretch body based on scroll difference
  - Rotate entire hold along line

- [ ] Visibility culling
  - Time window: [t - 0.5, t + approach]
  - Screen space: off-screen notes not rendered

**Optimization**: Reuse sprites instead of create/destroy

---

#### 2.8 Hit Effect Layer ⏳
**Tasks**:
- [ ] Sprite sheet animation for hit effects
- [ ] Particle burst on note hit
  - Color-matched to note type
  - Radial explosion pattern
- [ ] Timing: spawn on judge, auto-cleanup after animation

---

#### 2.9 UI Layer ⏳
**Tasks**:
- [ ] Score display (top right)
- [ ] Combo counter (center)
- [ ] Accuracy percentage
- [ ] FPS counter (debug)
- [ ] Progress bar (optional)

---

### Priority 3: Game Loop & Timing

#### 2.10 Timing Manager ⏳
**Tasks**:
- [ ] High-precision timing with AudioContext
  ```typescript
  class TimingManager {
    private audioContext: AudioContext
    private startTime: number
    private calibrationOffset: number = 0

    getCurrentTime(): number
    getCalibratedTime(): number
  }
  ```

- [ ] Synchronize with audio playback
- [ ] Apply calibration offset
- [ ] Handle playback speed changes

---

#### 2.11 Game Loop ⏳
**Tasks**:
- [ ] Main game loop with requestAnimationFrame
  ```typescript
  class GameLoop {
    start() {
      const tick = () => {
        const time = this.timing.getCalibratedTime()

        // Update game state
        this.updateNoteStates(time)
        this.judge.update(time)
        this.input.processEvents(time)

        // Render
        this.renderer.render(this.state, time)

        requestAnimationFrame(tick)
      }
      requestAnimationFrame(tick)
    }
  }
  ```

- [ ] Update note states (judged, hit, miss flags)
- [ ] Trigger hit effects
- [ ] Update UI (score, combo)

---

### Priority 4: Audio System

#### 2.12 Audio Manager ⏳
**Tasks**:
- [ ] Load and decode audio with Web Audio API
  ```typescript
  class AudioManager {
    async loadAudio(url: string): Promise<AudioBuffer>
    play(buffer: AudioBuffer, offset: number)
    pause()
    seek(time: number)
  }
  ```

- [ ] BGM playback
- [ ] Hitsound playback (tap, drag, flick)
- [ ] Volume control
- [ ] Sync timing with AudioContext.currentTime

---

### Priority 5: Calibration System (ENHANCEMENT)

#### 2.13 Auto-Calibration ⏳
**Tasks**:
- [ ] Record hit timing offsets
  ```typescript
  class CalibrationTool {
    private offsetHistory: number[] = []

    recordHit(result: JudgeResult) {
      this.offsetHistory.push(result.earlyLateOffset)
    }

    getSuggestedOffset(): number {
      // Return median offset
    }
  }
  ```

- [ ] Calculate median offset (robust against outliers)
- [ ] Suggest calibration adjustment
- [ ] Apply offset to TimingManager

#### 2.14 Manual Calibration UI ⏳
**Tasks**:
- [ ] Tap test component
  - Visual/audio metronome
  - User taps along
  - Calculate average offset

- [ ] Offset slider
  - Range: -200ms to +200ms
  - Real-time preview

- [ ] Calibration presets
  - Bluetooth headphones: +150ms typical
  - Wired headphones: +0ms
  - Built-in speakers: +50ms

---

## 🧪 Testing Strategy

### Unit Tests
- [ ] Kinematics: note positioning accuracy
- [ ] Judge: timing window edge cases
- [ ] Input: gesture recognition
- [ ] Tracks: interpolation accuracy

### Integration Tests
- [ ] Chart loading end-to-end
- [ ] Rendering pipeline
- [ ] Judge + input interaction

### Visual Regression Tests
- [ ] Screenshot comparison with Python renderer
- [ ] Frame-by-frame accuracy verification

---

## 📦 Asset Loading

### Texture Manager
- [ ] Load sprite sheets from respack
  - Click, Drag, Flick, Hold textures
  - Hit effect sprite sheet
- [ ] Create PIXI.Texture objects
- [ ] Handle multi-frame animations

### Resource Pack Format
- [ ] Read ZIP file
- [ ] Parse info.yml metadata
- [ ] Extract sprites and sounds
- [ ] **Reuse Python's respack.zip** (symlink)

---

## 🎨 UI Components (React)

### Game Canvas Component
```tsx
function GameCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const rendererRef = useRef<PixiRenderer>()

  useEffect(() => {
    const renderer = new PixiRenderer(canvasRef.current!)
    rendererRef.current = renderer
    return () => renderer.destroy()
  }, [])

  return <canvas ref={canvasRef} />
}
```

### Chart Selector
- [ ] List available charts
- [ ] Show metadata (name, artist, difficulty, BPM)
- [ ] Preview background
- [ ] Load chart on select

### Settings Panel
- [ ] Approach rate slider
- [ ] Note size slider
- [ ] Calibration offset
- [ ] Visual effects toggle

### Calibration Tool
- [ ] Tap test interface
- [ ] Offset history graph
- [ ] Apply/reset buttons

---

## 🔗 Backend Integration

### Chart API
- [ ] `GET /api/charts` - List charts
- [ ] `POST /api/charts/upload` - Upload chart
- [ ] `GET /api/charts/:id` - Get chart data
- [ ] `DELETE /api/charts/:id` - Delete chart

### Serve Python Assets
- [ ] Symlink `charts/` directory
- [ ] Serve via `/assets/charts/`
- [ ] Serve `respack.zip` via `/assets/respacks/`

---

## 📝 Implementation Order (Recommended)

1. **Week 1**: Core Runtime
   - [ ] Port kinematics.ts
   - [ ] Port Judge.ts
   - [ ] Create InputHandler.ts
   - [ ] Write unit tests

2. **Week 2**: PixiJS Foundation
   - [ ] Set up PixiJS Application
   - [ ] Implement BackgroundLayer
   - [ ] Implement JudgmentLineLayer
   - [ ] Basic rendering pipeline

3. **Week 3**: Note Rendering
   - [ ] Implement NoteLayer with sprite pooling
   - [ ] Hold note rendering
   - [ ] Visibility culling
   - [ ] Visual testing vs Python

4. **Week 4**: Interactivity
   - [ ] Connect InputHandler to judge
   - [ ] Hit effect layer
   - [ ] Game loop with timing
   - [ ] UI components

5. **Week 5**: Audio & Polish
   - [ ] AudioManager with Web Audio API
   - [ ] Calibration system
   - [ ] Settings panel
   - [ ] Performance optimization

6. **Week 6**: Backend & Integration
   - [ ] Chart API endpoints
   - [ ] File upload handling
   - [ ] Chart selector UI
   - [ ] End-to-end testing

---

## 🚀 Quick Start Commands

```bash
# Start development
cd phic_web
pnpm dev

# Backend: http://localhost:3000
# Frontend: http://localhost:5173

# Run tests
pnpm test

# Build for production
pnpm build
```

---

## 🎓 Critical Files to Reference

| File | Purpose | Priority |
|------|---------|----------|
| `phic_renderer/runtime/kinematics.py` | Note positioning | ⭐⭐⭐⭐⭐ |
| `phic_renderer/runtime/judge.py` | Hit detection | ⭐⭐⭐⭐⭐ |
| `phic_renderer/renderer/pygame/manual_judgement.py` | Input handling | ⭐⭐⭐⭐⭐ |
| `phic_renderer/renderer/pygame/frame_renderer.py` | Rendering pipeline | ⭐⭐⭐⭐ |
| `phic_renderer/renderer/pygame/hold.py` | Hold rendering | ⭐⭐⭐⭐ |
| `phic_renderer/renderer/pygame/hitfx.py` | Hit effects | ⭐⭐⭐ |

---

## 💡 Key Challenges & Solutions

### Challenge 1: Note Positioning Accuracy
**Problem**: Notes must appear at exact same positions as Python renderer
**Solution**: Port `noteWorldPos()` exactly, validate with unit tests

### Challenge 2: Timing Precision
**Problem**: Rhythm games require sub-millisecond accuracy
**Solution**: Use `AudioContext.currentTime` instead of `performance.now()`

### Challenge 3: Performance with 1000+ Notes
**Problem**: Creating/destroying sprites causes GC pressure
**Solution**: Sprite pooling - reuse sprites instead of recreate

### Challenge 4: Multi-Touch on Web
**Problem**: Web touch events are more complex than desktop
**Solution**: Use Pointer Events API for unified mouse/touch handling

---

## 🎯 Success Criteria

### Functionality
- ✅ Load Official format charts correctly
- ✅ Render notes at correct positions (visual match with Python)
- ✅ Accurate hit detection (±1ms precision)
- ✅ Smooth 60 FPS rendering
- ✅ Multi-touch input working on mobile

### Performance
- ✅ < 100ms load time for typical chart
- ✅ 60 FPS with 1000+ notes on screen
- ✅ < 50ms input latency

### Accuracy
- ✅ Math output matches Python (±0.001)
- ✅ Visual output matches Python renderer
- ✅ Timing matches Python judge system

---

## 📚 Resources

- **PixiJS v8 Docs**: https://pixijs.com/8.x/guides
- **Web Audio API**: https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API
- **Pointer Events**: https://developer.mozilla.org/en-US/docs/Web/API/Pointer_events
- **Python Reference**: `/Users/yanyige/PycharmProjects/MinimalPhigrosRend/phic_renderer/`
