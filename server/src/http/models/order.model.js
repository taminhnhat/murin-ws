const mongoose = require("mongoose");
mongoose.set("strictQuery", true);

const orderSchema = new mongoose.Schema({
  orderId: {
    type: String,
    required: true,
  },
  items: {
    type: Array,
    required: true,
    default: [],
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
});

module.exports = mongoose.model("order", orderSchema);
