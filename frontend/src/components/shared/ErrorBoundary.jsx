import React from 'react';

export default class ErrorBoundary extends React.Component {
  constructor(props) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error) {
    console.error('Panel error:', error);
  }

  render() {
    if (this.state.hasError) {
      return <div className="panel-error">PANEL ERROR</div>;
    }

    return this.props.children;
  }
}
