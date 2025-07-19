const webpack = require('webpack');
const path = require('path');

module.exports = {
  mode: 'production',
  entry: './nbt-bundler.js',
  output: {
    path: path.resolve(__dirname, 'dist'),
    filename: 'prismarine-nbt.min.js',
    library: {
      type: 'module',
    },
  },
  experiments: {
    outputModule: true,
  },
  resolve: {
    fallback: {
      // prismarine-nbt uses zlib for compressed NBT data
      zlib: require.resolve('browserify-zlib'),
      // other node polyfills that might be needed
      stream: require.resolve('stream-browserify'),
      buffer: require.resolve('buffer/'),
      events: require.resolve('events/'),
      assert: require.resolve('assert/'),
    },
  },
  plugins: [
    // Polyfill for process.env
    new webpack.ProvidePlugin({
      process: 'process/browser',
    }),
    // Polyfill for Buffer
    new webpack.ProvidePlugin({
      Buffer: ['buffer', 'Buffer'],
    }),
  ],
};