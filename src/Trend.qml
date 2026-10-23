import QtQuick
Rectangle {
    color: "white"
    Canvas {
        id: chart
        anchors.fill: parent
        Connections { target: trend; function onPlotChanged() { chart.requestPaint() } }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            let ctx = getContext("2d"); ctx.reset();
            let curves = trend.curves, end = 0, low = Infinity, high = -Infinity;
            for (let c of curves) for (let p of c.points) end = Math.max(end, p.x);
            let start = end - trend.timeWindow;
            for (let c of curves) if (c.visible) for (let p of c.points) if (p.x >= start) {
                low = Math.min(low, p.y); high = Math.max(high, p.y);
            }
            if (!isFinite(low)) { low = 0; high = 1; }
            if (high === low) { low -= 1; high += 1; }
            let margin = (high - low) * 0.05; low -= margin; high += margin;
            let left = 85, right = width - 22, top = 32, bottom = height - 55;
            ctx.font = "12px sans-serif"; ctx.fillStyle = "#334155";
            ctx.fillText("实际值（单位见图例）", left, 18);
            for (let i = 0; i <= 4; ++i) {
                let y = top + (bottom-top)*i/4;
                ctx.strokeStyle = "#dbe2ea"; ctx.beginPath(); ctx.moveTo(left,y); ctx.lineTo(right,y); ctx.stroke();
                ctx.fillText((high-(high-low)*i/4).toPrecision(5), 4, y+4);
                let x = left+(right-left)*i/4;
                ctx.fillText(((-trend.timeWindow+trend.timeWindow*i/4)).toFixed(1),x-15,bottom+21);
            }
            ctx.fillText("相对最新样本时间 / 秒", left, height-8);
            for (let c of curves) if (c.visible) {
                ctx.strokeStyle = c.color; ctx.fillStyle = c.color; ctx.lineWidth=2; ctx.beginPath(); let first=true;
                for (let p of c.points) if (p.x >= start) {
                    let x=left+(p.x-start)/trend.timeWindow*(right-left), y=bottom-(p.y-low)/(high-low)*(bottom-top);
                    if (first) { ctx.moveTo(x,y); first=false; } else ctx.lineTo(x,y);
                    ctx.fillRect(x-2,y-2,4,4);
                }
                ctx.stroke();
            }
        }
    }
}
