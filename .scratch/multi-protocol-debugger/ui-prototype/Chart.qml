import QtQuick

Rectangle {
    id: root
    color: "#111d30"
    property var series: []
    property var names: []
    property var palette: ["#53d5b6", "#66afff", "#ffca75", "#e09bff"]
    property int windowSize: 60
    property bool paused: false
    function appendSample(values) {
        if (paused) return
        let data = series.slice()
        for (let i = 0; i < values.length; i++) {
            if (!data[i]) data[i] = []
            data[i] = data[i].concat([Number(values[i])]).slice(-300)
        }
        series = data
        plot.requestPaint()
    }
    function clearSamples() { series = []; plot.requestPaint() }
    function zoomBy(factor) { windowSize = Math.max(10, Math.min(300, Math.round(windowSize * factor))); plot.requestPaint() }
    onWidthChanged: plot.requestPaint()
    onHeightChanged: plot.requestPaint()
    Row {
        anchors.top: parent.top; anchors.left: parent.left
        anchors.margins: 22; spacing: 24
        Repeater {
            model: root.names
            delegate: Text { required property int index; required property string modelData
                text: "● " + modelData; color: root.palette[index % 4]; font.pixelSize: 13
            }
        }
    }
    Canvas {
        id: plot
        anchors.fill: parent; anchors.margins: 22; anchors.topMargin: 64; anchors.bottomMargin: 44
        onPaint: {
            let c = getContext("2d")
            c.clearRect(0, 0, width, height)
            c.strokeStyle = "#29374d"; c.lineWidth = 1
            for (let k = 0; k <= 5; k++) { c.beginPath(); c.moveTo(0, k * height / 5); c.lineTo(width, k * height / 5); c.stroke() }
            for (let k = 0; k <= 8; k++) { c.beginPath(); c.moveTo(k * width / 8, 0); c.lineTo(k * width / 8, height); c.stroke() }
            for (let s = 0; s < root.series.length; s++) {
                let values = root.series[s].slice(-root.windowSize)
                if (!values.length) continue
                let low = Math.min.apply(null, values), high = Math.max.apply(null, values)
                let pad = Math.max((high - low) * 0.15, 0.5)
                low -= pad; high += pad
                c.strokeStyle = root.palette[s % 4]; c.lineWidth = 2; c.beginPath()
                for (let i = 0; i < values.length; i++) {
                    let x = i * width / Math.max(root.windowSize - 1, 1)
                    let y = height - (values[i] - low) / (high - low) * height
                    if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
                }
                c.stroke()
                c.fillStyle = root.palette[s % 4]; c.font = "12px sans-serif"
                c.fillText(Number(values[values.length - 1]).toFixed(2), width - 68, 18 + s * 20)
            }
        }
        MouseArea {
            anchors.fill: parent
            onWheel: function(wheel) { root.zoomBy(wheel.angleDelta.y > 0 ? 0.8 : 1.25) }
        }
    }
    Text {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 18
        text: "最近 " + root.windowSize + " 个采样点 · 各曲线独立自动量程，仅比较趋势 · 滚轮缩放"
        color: "#9aabc3"; font.pixelSize: 12
    }
}
