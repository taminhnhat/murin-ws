const mongoose = require("mongoose");
mongoose.set("strictQuery", true);

const robotSchema = new mongoose.Schema({
  botId: {
    type: String,
    required: true,
  },
  statusCode: {
    type: Number,
    required: true,
    default: 600,
  },
  statusText: {
    type: String,
    required: true,
    default: "in queue",
  },
  routingPlan: {},
});

module.exports = mongoose.model("order", robotSchema);
