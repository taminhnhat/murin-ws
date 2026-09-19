const express = require("express");
const router = express.Router();

router.post("/order/add", (req, res) => {
  res.status(200).json({
    status: "success",
  });
});
router.get("/", (req, res) => {
  res.sendFile(__dirname + "/controller.html");
});

module.exports = router;
