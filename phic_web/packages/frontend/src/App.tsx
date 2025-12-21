import { useRef, useState, useEffect } from 'react'
import { GameLoop } from './game/GameLoop.js'
import { loadChart, type LoadedChart } from './loaders/ChartLoader.js'
import { loadRespack, type Respack } from './loaders/RespackLoader.js'
import { loadChartPack } from './loaders/ChartPackLoader.js'

type GameStatus = 'idle' | 'loading' | 'ready' | 'playing' | 'paused' | 'finished'

function App() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const gameLoopRef = useRef<GameLoop | null>(null)
  const [status, setStatus] = useState<GameStatus>('idle')
  const [error, setError] = useState<string | null>(null)

  // Loaded files
  const [loadedChart, setLoadedChart] = useState<LoadedChart | null>(null)
  const [respack, setRespack] = useState<Respack | null>(null)
  const [bgmFile, setBgmFile] = useState<File | null>(null)
  const [backgroundFile, setBackgroundFile] = useState<File | null>(null)
  const [bgmBlob, setBgmBlob] = useState<Blob | null>(null)
  const [backgroundBlob, setBackgroundBlob] = useState<Blob | null>(null)

  // Game state
  const [score, setScore] = useState(0)
  const [combo, setCombo] = useState(0)
  const [accuracy, setAccuracy] = useState(100)

  // Settings
  const [showPointerFeedback, setShowPointerFeedback] = useState(true)
  const [autoPlay, setAutoPlay] = useState(false)
  const [noteSizeMult, setNoteSizeMult] = useState(1.0)

  // Pointer feedback state
  const [activePointers, setActivePointers] = useState<Map<number, { x: number; y: number }>>(
    new Map()
  )

  // File handlers
  const handleChartFile = async (file: File) => {
    try {
      setStatus('loading')
      setError(null)
      const chart = await loadChart(file, 1920, 1080)
      setLoadedChart(chart)
      setStatus('idle')
    } catch (err) {
      setError(`Failed to load chart: ${err}`)
      setStatus('idle')
    }
  }

  const handleChartPackFile = async (file: File) => {
    try {
      setStatus('loading')
      setError(null)
      const pack = await loadChartPack(file, 1920, 1080)
      setLoadedChart(pack.chart)
      setBgmBlob(pack.musicBlob)
      setBackgroundBlob(pack.backgroundBlob)
      // Clear individual file uploads when loading pack
      setBgmFile(null)
      setBackgroundFile(null)
      setStatus('idle')
    } catch (err) {
      setError(`Failed to load chart pack: ${err}`)
      setStatus('idle')
    }
  }

  const handleRespackFile = async (file: File) => {
    try {
      setStatus('loading')
      setError(null)
      const pack = await loadRespack(file)
      setRespack(pack)
      setStatus('idle')
    } catch (err) {
      setError(`Failed to load respack: ${err}`)
      setStatus('idle')
    }
  }

  const handleBgmFile = (file: File) => {
    setBgmFile(file)
    setBgmBlob(null) // Clear blob when user uploads file
  }

  const handleBackgroundFile = (file: File) => {
    setBackgroundFile(file)
    setBackgroundBlob(null) // Clear blob when user uploads file
  }

  // Game controls
  const handlePlay = async () => {
    if (!loadedChart || !canvasRef.current) {
      setError('Please load a chart first')
      return
    }

    try {
      setStatus('loading')
      setError(null)

      // Create URLs for files (prioritize files over blobs)
      const bgmUrl = bgmFile
        ? URL.createObjectURL(bgmFile)
        : bgmBlob
        ? URL.createObjectURL(bgmBlob)
        : undefined
      const backgroundUrl = backgroundFile
        ? URL.createObjectURL(backgroundFile)
        : backgroundBlob
        ? URL.createObjectURL(backgroundBlob)
        : undefined

      console.log('[GameStart] Creating GameLoop...')
      // Create game loop
      const gameLoop = new GameLoop(canvasRef.current, loadedChart.chart, {
        width: 1920,
        height: 1080,
        bgmUrl,
        backgroundUrl,
        respack: respack ?? undefined,
        chartSpeed: 1.0,
        audioOffset: 0,
        bgmVolume: 0.8,
        autoPlay,
        noteSizeMult,
      })

      gameLoopRef.current = gameLoop

      console.log('[GameStart] Setting up pointer events...')
      // Set up pointer event handlers with visual feedback
      const canvas = canvasRef.current

      const handlePointerDown = (e: PointerEvent) => {
        gameLoop.onPointerDown(e)
        if (showPointerFeedback) {
          const rect = canvas.getBoundingClientRect()
          setActivePointers((prev) => {
            const next = new Map(prev)
            next.set(e.pointerId, {
              x: e.clientX - rect.left,
              y: e.clientY - rect.top
            })
            return next
          })
        }
      }

      const handlePointerUp = (e: PointerEvent) => {
        gameLoop.onPointerUp(e)
        if (showPointerFeedback) {
          setActivePointers((prev) => {
            const next = new Map(prev)
            next.delete(e.pointerId)
            return next
          })
        }
      }

      const handlePointerMove = (e: PointerEvent) => {
        gameLoop.onPointerMove(e)
        if (showPointerFeedback && activePointers.has(e.pointerId)) {
          const rect = canvas.getBoundingClientRect()
          setActivePointers((prev) => {
            const next = new Map(prev)
            next.set(e.pointerId, {
              x: e.clientX - rect.left,
              y: e.clientY - rect.top
            })
            return next
          })
        }
      }

      canvas.addEventListener('pointerdown', handlePointerDown)
      canvas.addEventListener('pointerup', handlePointerUp)
      canvas.addEventListener('pointermove', handlePointerMove)

      console.log('[GameStart] Starting game loop...')
      // Start game loop
      await gameLoop.start()
      console.log('[GameStart] Game started successfully!')
      setStatus('playing')

      // Set up game state polling
      const interval = setInterval(() => {
        const state = gameLoop.getState()
        setScore(state.getScore())
        setCombo(state.getCombo())
        setAccuracy(state.getAccuracy())

        // Check if game finished
        if (state.isComplete()) {
          setStatus('finished')
          clearInterval(interval)
        }
      }, 100)

      // Clean up on unmount
      return () => {
        clearInterval(interval)
      }
    } catch (err) {
      setError(`Failed to start game: ${err}`)
      setStatus('idle')
    }
  }

  const handlePause = () => {
    if (gameLoopRef.current && status === 'playing') {
      gameLoopRef.current.pause()
      setStatus('paused')
    }
  }

  const handleResume = () => {
    if (gameLoopRef.current && status === 'paused') {
      gameLoopRef.current.resume()
      setStatus('playing')
    }
  }

  const handleRestart = async () => {
    if (gameLoopRef.current) {
      gameLoopRef.current.destroy()
      gameLoopRef.current = null
    }
    setStatus('idle')
    setScore(0)
    setCombo(0)
    setAccuracy(100)
  }

  // Clean up on unmount
  useEffect(() => {
    return () => {
      if (gameLoopRef.current) {
        gameLoopRef.current.destroy()
      }
    }
  }, [])

  const canStart = loadedChart !== null && (status === 'idle' || status === 'loading')
  const isPlaying = status === 'playing' || status === 'paused'

  return (
    <div className="min-h-screen bg-gray-900 text-white">
      {/* Header */}
      <div className="bg-gray-800 border-b border-gray-700 p-4">
        <h1 className="text-2xl font-bold">Phigros Web Player</h1>
        <p className="text-sm text-gray-400">Interactive browser-based chart player</p>
      </div>

      <div className="flex h-[calc(100vh-80px)]">
        {/* Sidebar */}
        <div className="w-80 bg-gray-800 border-r border-gray-700 p-4 overflow-y-auto">
          <h2 className="text-lg font-semibold mb-4">Load Files</h2>

          {/* Error display */}
          {error && (
            <div className="bg-red-900 border border-red-700 rounded p-3 mb-4 text-sm">
              {error}
            </div>
          )}

          {/* Chart Pack (All-in-one) */}
          <div className="mb-4 p-3 bg-blue-900/20 border border-blue-700/50 rounded">
            <label className="block text-sm font-medium mb-2">
              Chart Pack (ZIP) <span className="text-blue-400">- Recommended</span>
            </label>
            <input
              type="file"
              accept=".zip,.pez"
              onChange={(e) => e.target.files?.[0] && handleChartPackFile(e.target.files[0])}
              disabled={isPlaying}
              className="w-full text-sm text-gray-400 file:mr-4 file:py-2 file:px-4 file:rounded file:border-0 file:text-sm file:font-semibold file:bg-blue-600 file:text-white hover:file:bg-blue-700 disabled:opacity-50"
            />
            <p className="mt-1 text-xs text-gray-400">
              ZIP file containing chart, music, and background
            </p>
          </div>

          <div className="mb-4 text-center text-xs text-gray-500">
            OR load files individually:
          </div>

          {/* Chart file */}
          <div className="mb-4">
            <label className="block text-sm font-medium mb-2">
              Chart File <span className="text-red-500">*</span>
            </label>
            <input
              type="file"
              accept=".json,.pec,.pe"
              onChange={(e) => e.target.files?.[0] && handleChartFile(e.target.files[0])}
              disabled={isPlaying}
              className="w-full text-sm text-gray-400 file:mr-4 file:py-2 file:px-4 file:rounded file:border-0 file:text-sm file:font-semibold file:bg-blue-600 file:text-white hover:file:bg-blue-700 disabled:opacity-50"
            />
            {loadedChart && (
              <div className="mt-2 text-xs text-green-400">
                ✓ {loadedChart.fileName} ({loadedChart.format})
              </div>
            )}
          </div>

          {/* Respack file */}
          <div className="mb-4">
            <label className="block text-sm font-medium mb-2">
              Resource Pack (Optional)
            </label>
            <input
              type="file"
              accept=".zip"
              onChange={(e) => e.target.files?.[0] && handleRespackFile(e.target.files[0])}
              disabled={isPlaying}
              className="w-full text-sm text-gray-400 file:mr-4 file:py-2 file:px-4 file:rounded file:border-0 file:text-sm file:font-semibold file:bg-gray-600 file:text-white hover:file:bg-gray-700 disabled:opacity-50"
            />
            {respack && (
              <div className="mt-2 text-xs text-green-400">
                ✓ Resource pack loaded
              </div>
            )}
          </div>

          {/* BGM file */}
          <div className="mb-4">
            <label className="block text-sm font-medium mb-2">
              BGM (Optional)
            </label>
            <input
              type="file"
              accept=".mp3,.ogg,.wav"
              onChange={(e) => e.target.files?.[0] && handleBgmFile(e.target.files[0])}
              disabled={isPlaying}
              className="w-full text-sm text-gray-400 file:mr-4 file:py-2 file:px-4 file:rounded file:border-0 file:text-sm file:font-semibold file:bg-gray-600 file:text-white hover:file:bg-gray-700 disabled:opacity-50"
            />
            {bgmFile && (
              <div className="mt-2 text-xs text-green-400">
                ✓ {bgmFile.name}
              </div>
            )}
            {bgmBlob && !bgmFile && (
              <div className="mt-2 text-xs text-green-400">
                ✓ Music from chart pack
              </div>
            )}
          </div>

          {/* Background file */}
          <div className="mb-6">
            <label className="block text-sm font-medium mb-2">
              Background (Optional)
            </label>
            <input
              type="file"
              accept=".jpg,.jpeg,.png"
              onChange={(e) => e.target.files?.[0] && handleBackgroundFile(e.target.files[0])}
              disabled={isPlaying}
              className="w-full text-sm text-gray-400 file:mr-4 file:py-2 file:px-4 file:rounded file:border-0 file:text-sm file:font-semibold file:bg-gray-600 file:text-white hover:file:bg-gray-700 disabled:opacity-50"
            />
            {backgroundFile && (
              <div className="mt-2 text-xs text-green-400">
                ✓ {backgroundFile.name}
              </div>
            )}
            {backgroundBlob && !backgroundFile && (
              <div className="mt-2 text-xs text-green-400">
                ✓ Background from chart pack
              </div>
            )}
          </div>

          {/* Controls */}
          <div className="space-y-2">
            {!isPlaying && (
              <button
                onClick={handlePlay}
                disabled={!canStart || status === 'loading'}
                className="w-full bg-green-600 hover:bg-green-700 disabled:bg-gray-600 disabled:cursor-not-allowed text-white font-semibold py-2 px-4 rounded transition"
              >
                {status === 'loading' ? 'Loading...' : 'Play'}
              </button>
            )}

            {status === 'playing' && (
              <button
                onClick={handlePause}
                className="w-full bg-yellow-600 hover:bg-yellow-700 text-white font-semibold py-2 px-4 rounded transition"
              >
                Pause
              </button>
            )}

            {status === 'paused' && (
              <button
                onClick={handleResume}
                className="w-full bg-green-600 hover:bg-green-700 text-white font-semibold py-2 px-4 rounded transition"
              >
                Resume
              </button>
            )}

            {(isPlaying || status === 'finished') && (
              <button
                onClick={handleRestart}
                className="w-full bg-red-600 hover:bg-red-700 text-white font-semibold py-2 px-4 rounded transition"
              >
                Stop
              </button>
            )}
          </div>

          {/* Settings */}
          <div className="mt-4 p-3 bg-gray-700/50 rounded">
            <h3 className="text-sm font-semibold mb-2">Settings</h3>
            <label className="flex items-center space-x-2 text-sm cursor-pointer">
              <input
                type="checkbox"
                checked={showPointerFeedback}
                onChange={(e) => setShowPointerFeedback(e.target.checked)}
                className="w-4 h-4 text-blue-600 bg-gray-700 border-gray-600 rounded focus:ring-blue-500"
              />
              <span>Show touch feedback</span>
            </label>

            <label className="mt-3 flex items-center space-x-2 text-sm cursor-pointer">
              <input
                type="checkbox"
                checked={autoPlay}
                onChange={(e) => setAutoPlay(e.target.checked)}
                disabled={isPlaying}
                className="w-4 h-4 text-blue-600 bg-gray-700 border-gray-600 rounded focus:ring-blue-500"
              />
              <span>Autoplay</span>
            </label>

            <div className="mt-3 text-sm">
              <label className="block text-xs text-gray-300 mb-1">Note size multiplier</label>
              <input
                type="number"
                step="0.05"
                min="0.1"
                max="5"
                value={noteSizeMult}
                onChange={(e) => setNoteSizeMult(parseFloat(e.target.value || '1'))}
                disabled={isPlaying}
                className="w-full px-2 py-1 rounded bg-gray-800 border border-gray-600 text-white"
              />
            </div>
          </div>

          {/* Stats */}
          {isPlaying && (
            <div className="mt-6 p-4 bg-gray-700 rounded">
              <h3 className="text-sm font-semibold mb-3">Current Stats</h3>
              <div className="space-y-2 text-sm">
                <div className="flex justify-between">
                  <span className="text-gray-400">Score:</span>
                  <span className="font-mono">{score.toLocaleString()}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-gray-400">Combo:</span>
                  <span className="font-mono">{combo}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-gray-400">Accuracy:</span>
                  <span className="font-mono">{accuracy.toFixed(2)}%</span>
                </div>
              </div>
            </div>
          )}

          {/* Results */}
          {status === 'finished' && (
            <div className="mt-6 p-4 bg-blue-900 border border-blue-700 rounded">
              <h3 className="text-lg font-semibold mb-3">Results</h3>
              <div className="space-y-2">
                <div className="flex justify-between text-lg">
                  <span>Score:</span>
                  <span className="font-bold">{score.toLocaleString()}</span>
                </div>
                <div className="flex justify-between">
                  <span>Accuracy:</span>
                  <span className="font-bold">{accuracy.toFixed(2)}%</span>
                </div>
                <div className="flex justify-between">
                  <span>Max Combo:</span>
                  <span className="font-bold">{combo}</span>
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Main canvas area */}
        <div className="flex-1 bg-black flex items-center justify-center relative">
          <div className="relative">
            <canvas
              ref={canvasRef}
              className="max-w-full max-h-full"
              style={{ touchAction: 'none' }}
            />

            {/* Pointer feedback overlay */}
            {showPointerFeedback && (
              <div className="absolute inset-0 pointer-events-none">
                {Array.from(activePointers.entries()).map(([id, pos]) => (
                  <div
                    key={id}
                    className="absolute w-16 h-16 rounded-full border-4 border-white/50 animate-ping"
                    style={{
                      left: pos.x - 32,
                      top: pos.y - 32,
                    }}
                  />
                ))}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  )
}

export default App
