const express = require("express");
const path = require("path");
const app = express();
const PORT = process.env.PORT || 3000;

// serve static assets
app.use(express.static(path.join(__dirname, "public")));

// dashboard page
app.get("/", (req, res) => {
  res.sendFile(path.join(__dirname, "public", "dashboard.html"));
});

// analytics page
app.get("/analytics", (req, res) => {
  res.sendFile(path.join(__dirname, "public", "analytics.html"));
});

app.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});
