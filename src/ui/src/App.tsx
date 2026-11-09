import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom'
import AuthProvider from './components/AuthProvider'
import Ruta from './components/Ruta'
import Dominios from './pages/Dominios'
import Entrar from './pages/Entrar'
import Login from './pages/Login'
import Urls from './pages/Urls'

export default function App() {
  return (
    <AuthProvider>
      <BrowserRouter>
        <Routes>
          <Route path="/entrar" element={<Entrar />} />
          {/* Pública: la usa quien visita una URL protegida, no quien administra la cuenta */}
          <Route path="/login" element={<Login />} />
          <Route path="/" element={<Ruta><Dominios /></Ruta>} />
          <Route path="/dominios/:id" element={<Ruta><Urls /></Ruta>} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </AuthProvider>
  )
}
