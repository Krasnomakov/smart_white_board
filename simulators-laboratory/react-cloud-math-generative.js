import React, { useState, useEffect } from 'react';

const DreamGenerator = () => {
  const [time, setTime] = useState(0);
  
  useEffect(() => {
    const interval = setInterval(() => {
      setTime(t => (t + 1) % 1000);
    }, 50);
    return () => clearInterval(interval);
  }, []);

  const generateSwirlPath = (centerX, centerY, radius, rotations) => {
    let path = `M ${centerX} ${centerY}`;
    for (let i = 0; i <= 360 * rotations; i += 5) {
      const angle = (i * Math.PI) / 180;
      const r = radius * (1 - i / (360 * rotations));
      const x = centerX + r * Math.cos(angle);
      const y = centerY + r * Math.sin(angle);
      path += ` L ${x} ${y}`;
    }
    return path;
  };

  const generateCloud = (x, y, scale) => {
    const baseRadius = 30 * scale;
    const centers = [
      [x, y],
      [x - baseRadius, y + baseRadius * 0.5],
      [x + baseRadius, y + baseRadius * 0.5],
      [x - baseRadius * 2, y + baseRadius],
      [x + baseRadius * 2, y + baseRadius],
    ];

    return centers.map((center, i) => (
      <circle
        key={i}
        cx={center[0]}
        cy={center[1]}
        r={baseRadius * (0.8 + 0.2 * Math.sin(time / 20 + i))}
        fill={`hsl(32, 100%, ${50 + 20 * Math.sin(time / 30 + i)}%)`}
        opacity={0.8}
      />
    ));
  };

  return (
    <div className="w-full h-full bg-indigo-900">
      <svg viewBox="0 0 800 600" className="w-full h-full">
        {/* Background swirls */}
        {[...Array(5)].map((_, i) => (
          <path
            key={i}
            d={generateSwirlPath(
              400 + 100 * Math.sin(time / 100 + i),
              300 + 50 * Math.cos(time / 120 + i),
              150 - i * 20,
              2
            )}
            fill="none"
            stroke={`hsl(${200 + i * 20}, 70%, ${60 + 10 * Math.sin(time / 50 + i)}%)`}
            strokeWidth="2"
            opacity={0.3}
          />
        ))}

        {/* Mathematical pattern overlay */}
        <g transform={`translate(${400 + 50 * Math.sin(time / 200)} ${300 + 30 * Math.cos(time / 180)})`}>
          {[...Array(8)].map((_, i) => {
            const angle = (i * Math.PI * 2) / 8 + time / 100;
            return (
              <path
                key={i}
                d={`M 0 0 L ${100 * Math.cos(angle)} ${100 * Math.sin(angle)}`}
                stroke={`hsl(${40 + i * 40}, 100%, 70%)`}
                strokeWidth="3"
                opacity={0.5}
              />
            );
          })}
        </g>

        {/* Animated clouds */}
        {[...Array(3)].map((_, i) => (
          <g key={i} transform={`translate(${50 * Math.sin(time / 150 + i * 2)} ${30 * Math.cos(time / 170 + i * 2)})`}>
            {generateCloud(200 + i * 200, 400 - i * 50, 1 - i * 0.2)}
          </g>
        ))}
      </svg>
    </div>
  );
};

export default DreamGenerator;